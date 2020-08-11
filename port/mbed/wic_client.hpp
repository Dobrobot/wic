/* Copyright (c) 2020 Cameron Harper
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * */

#include "mbed.h"
#include "wic.h"
#include "TLSSocket.h"
#include "wic_output_queue.hpp"

namespace WIC {

    class ClientBase {
    };

    template<size_t RX_MAX = 1000, size_t TX_MAX = 1012>
    class Client : public ClientBase {

        protected:

            uint8_t rx[RX_MAX];
            OutputQueue<TX_MAX> output;
            Mail<Buffer<RX_MAX>, 1> input;

            static const uint32_t socket_open_flag = 1U;

            NetworkInterface &interface;
            
            TCPSocket tcp;
            TLSSocketWrapper tls;
            Socket &socket;            
            Mutex mutex;
            ConditionVariable condition;
            EventQueue events;
            EventFlags flags;

            struct {

                int timeout_id;
                bool *done;
                enum wic_status *retval;
                BufferBase *buf;
                
            } job;

            Thread writer_thread;        
            Thread reader_thread;
            Thread event_thread;

            struct wic_inst inst;

            enum wic_schema schema;

            Callback<void(bool,const char *, uint16_t)> on_text_cb;
            Callback<void(bool,const void *, uint16_t)> on_binary_cb;
            Callback<void()> on_open_cb;
            Callback<void(uint16_t, const char *, uint16_t)> on_close_cb;

            static Client *to_obj(struct wic_inst *self)
            {       
                return static_cast<Client *>(wic_get_app(self));
            }

            static void handle_send(struct wic_inst *self, const void *data, size_t size, enum wic_frame_type type)
            {
                Client *obj = to_obj(self);

                if(obj->job.buf){

                    obj->job.buf->size = size;
                    obj->output.put(&obj->job.buf);
                }                
            }

            static void *handle_buffer(struct wic_inst *self, size_t size, enum wic_frame_type type, size_t *max)
            {
                void *retval = NULL;
                Client *obj = to_obj(self);

                obj->job.buf = obj->output.alloc(type, size);

                if(obj->job.buf){

                    *max = obj->job.buf->max;
                    retval = obj->job.buf->data;
                }

                return retval;
            }
            
            static uint32_t handle_rand(struct wic_inst *self)
            {
                return rand();
            }
            
            static void handle_handshake_failure(struct wic_inst *self, enum wic_handshake_failure reason)
            {
                Client *obj = to_obj(self);

                obj->events.cancel(obj->job.timeout_id);

                *obj->job.done = true;
                obj->condition.notify_all();
            }
            
            static void handle_open(struct wic_inst *self)
            {
                Client *obj = to_obj(self);

                obj->events.cancel(obj->job.timeout_id);
                
                if(obj->on_open_cb){

                    obj->on_open_cb();
                }
            }
            
            static void handle_text(struct wic_inst *self, bool fin, const char *data, uint16_t size)
            {
                Client *obj = to_obj(self);
                
                if(obj->on_text_cb){

                    obj->on_text_cb(fin, data, size);
                }    
            }
            
            static void handle_binary(struct wic_inst *self, bool fin, const void *data, uint16_t size)
            {
                Client *obj = to_obj(self);
                
                if(obj->on_binary_cb){

                    obj->on_binary_cb(fin, data, size);
                }   
            }

            static void handle_close(struct wic_inst *self, uint16_t code, const char *reason, uint16_t size)
            {
                Client *obj = to_obj(self);
        
                if(obj->on_close_cb){

                    obj->on_close_cb(code, reason, size);
                }
            }
            
            static void handle_close_transport(struct wic_inst *self)
            {
                to_obj(self)->socket.close();
            }
            
            void do_parse()
            {
                Buffer<RX_MAX> *buf;
                osEvent evt = input.get();

                if(evt.status == osEventMail){

                    buf = (Buffer<RX_MAX> *)evt.value.p;
                    
                    wic_parse(&inst, buf->data, buf->size);

                    input.free(buf);
                }
            }
            
            void do_open(bool &done, enum wic_status &retval, const char *url)
            {
                SocketAddress a;
                nsapi_error_t err;

                struct wic_init_arg init_arg = {0};

                init_arg.app = this;
                
                init_arg.rx = rx;
                init_arg.rx_max = sizeof(rx);

                init_arg.on_open = handle_open;
                init_arg.on_close = handle_close;
                init_arg.on_text = handle_text;
                init_arg.on_binary = handle_binary;
                init_arg.on_close_transport = handle_close_transport;
                init_arg.on_handshake_failure = handle_handshake_failure;

                init_arg.on_send = handle_send;
                init_arg.on_buffer = handle_buffer;
                init_arg.rand = handle_rand;
                
                init_arg.role = WIC_ROLE_CLIENT;
                
                init_arg.url = url;
                
                if(!wic_init(&inst, &init_arg)){

                    done = true;
                    condition.notify_all();
                    return;
                }

                schema = wic_get_url_schema(&inst);

                err = interface.gethostbyname(wic_get_url_hostname(&inst), &a);

                if(err != NSAPI_ERROR_OK){

                    done = true;
                    condition.notify_all();                    
                    return;
                }

                a.set_port(wic_get_url_port(&inst));

                switch(schema){
                default:
                case WIC_SCHEMA_HTTP:
                case WIC_SCHEMA_WS:
                    socket = tcp;                    
                    break;    
                case WIC_SCHEMA_HTTPS:    
                case WIC_SCHEMA_WSS:
                    socket = tls;
                    break;
                }

                err = tcp.open(&interface);

                err = tcp.connect(a);

                if(err != NSAPI_ERROR_OK){

                    socket.close();
                    condition.notify_all();
                    done = true;
                    return;
                }

                if(wic_start(&inst) != WIC_STATUS_SUCCESS){

                    done = true;
                    return;
                }

                //__asm__("BKPT");

                flags.set(socket_open_flag);

                job.timeout_id = events.call_in(5000, callback(this, &Client::do_handshake_timeout));
                job.done = &done;    
                job.retval = &retval;    
            }
            
            void do_close(bool &done)
            {
                wic_close(&inst);
                done = true;
                condition.notify_all();
            }
            
            void do_send_text(bool &done, enum wic_status &retval, bool fin, const char *value, uint16_t size)
            {
                retval = wic_send_text(&inst, fin, value, size);
                done = true;
                condition.notify_all();        
            }
            
            void do_send_binary(bool &done, enum wic_status &retval, bool fin, const void *value, uint16_t size)
            {
                retval = wic_send_binary(&inst, fin, value, size);
                done = true;
                condition.notify_all();        
            }

            void do_transport_error()
            {
                wic_close_with_reason(&inst, WIC_CLOSE_ABNORMAL_2, NULL, 0U);
            }
            
            void do_handshake_timeout()
            {
                wic_close_with_reason(&inst, WIC_CLOSE_ABNORMAL_1, NULL, 0U);
            }
            
            void writer_task(void)
            {
                BufferBase *buf;
                nsapi_size_or_error_t ret;
                
                for(;;){

                    flags.wait_any(socket_open_flag);

                    //__asm__("BKPT");

                    for(;;){

                        buf = output.get();

                        if(buf){

                            size_t pos = 0U;

                            do{

                                ret = socket.send(&buf->data[pos], buf->size - pos);

                                if(ret >= 0){

                                    pos += ret;
                                }
                            }
                            while((ret >= 0) && (pos < buf->size));

                            output.free(&buf);

                            if(ret < 0){

                                events.call(callback(this, &Client::do_transport_error));
                                break;
                            }                            
                        }
                    }

                    flags.clear(socket_open_flag);
                }
            }
            
            void reader_task(void)
            {
                nsapi_size_or_error_t retval;
                
                for(;;){

                    flags.wait_any(socket_open_flag);

                    for(;;){

                        auto buf = input.alloc();

                        retval = socket.recv(buf->data, sizeof(buf->data));

                        if(retval < 0){

                            events.call(callback(this, &Client::do_transport_error));

                            input.free(buf);    
                            break;
                        }
                        else{
                        
                            buf->size = retval;

                            input.put(buf);
                            
                            events.call(this, &Client::do_parse);
                        }
                    }

                    flags.clear(socket_open_flag);
                }
            }
     
        public:

            Client(NetworkInterface &interface) :
                interface(interface),
                tls(&tcp),
                socket(tcp),
                condition(mutex),
                schema(WIC_SCHEMA_WS)
            {                
                writer_thread.start(callback(this, &Client::writer_task));
                reader_thread.start(callback(this, &Client::reader_task));
                event_thread.start(callback(&events, &EventQueue::dispatch_forever));
            }

            enum wic_status open(const char *url)
            {
                printf("open()\n");

                enum wic_status retval = WIC_STATUS_SUCCESS;
                bool done = false;
                
                mutex.lock();

                printf("enqueue()\n");

                events.call(callback(this, &Client::do_open), done, retval, url);

                while(!done){

                    printf("waiting...\n");
                
                    condition.wait();
                }

                mutex.unlock();

                printf("done!\n");

                return retval;
            }

            void close()
            {
                mutex.lock();

                bool done = false;

                events.call(callback(this, &Client::do_close), done);

                while(!done){
                
                    condition.wait();
                }

                mutex.unlock();
            }

            enum wic_status text(const char *value)
            {
                return text(true, value);
            }
            
            enum wic_status text(const char *value, uint16_t size)
            {
                return text(true, value, size);
            }
            
            enum wic_status text(bool fin, const char *value)
            {
                enum wic_status retval = WIC_STATUS_SUCCESS;
                int size = strlen(value);

                if(size >= 0){

                    retval = text(fin, value, (uint16_t)size);
                }

                return retval;
            }
            
            enum wic_status text(bool fin, const char *value, uint16_t size)
            {
                enum wic_status retval = WIC_STATUS_SUCCESS;
                bool done = false;

                mutex.lock();

                //while(output.full_for(WIC_FRAME_TYPE_USER)){

                  //  condition.wait();
                //}

                events.call(callback(this, &Client::do_send_text), done, retval, fin, value, size);

                while(!done){
                
                    condition.wait();
                }

                mutex.unlock();

                return retval;
            }

            enum wic_status binary(const void *value, uint16_t size)
            {
                return binary(true, value, size);
            }
            
            enum wic_status binary(bool fin, const void *value, uint16_t size)
            {
                enum wic_status retval;
                bool done = false;

                mutex.lock();

                while(output.full_for(WIC_FRAME_TYPE_USER)){

                    condition.wait();
                }

                events.call(callback(this, &Client::do_send_binary), done, retval, fin, value, size);

                while(!done){
                
                    condition.wait();
                }

                mutex.unlock();

                return retval;
            }

            bool is_open()
            {
                return(wic_get_state(&inst) == WIC_STATE_OPEN);
            }

            void on_text(Callback<void(bool,const char *, uint16_t)> handler)
            {
                on_text_cb = handler;
            }

            void on_binary(Callback<void(bool,const void *, uint16_t)> handler)
            {
                on_binary_cb = handler;
            }

            void on_open(Callback<void()> handler)
            {
                on_open_cb = handler;
            }

            void on_close(Callback<void(uint16_t, const char *, uint16_t)> handler)
            {
                on_close_cb = handler;
            }

            nsapi_error_t set_root_ca_cert(const void *root_ca, size_t len)
            {
                return tls.set_root_ca_cert(root_ca, len);
            }

            nsapi_error_t set_root_ca_cert(const char *root_ca_pem)
            {
                return tls.set_root_ca_cert(root_ca_pem);
            }

            nsapi_error_t set_client_cert_key(const char *client_cert_pem, const char *client_private_key_pem)
            {
                return tls.set_client_cert_key(client_cert_pem, client_private_key_pem);
            }

            nsapi_error_t set_client_cert_key(const void *client_cert_pem, size_t client_cert_len, const void *client_private_key_pem, size_t client_private_key_len)
            {
                return tls.set_client_cert_key(client_cert_pem, client_cert_len, client_private_key_pem, client_private_key_len);
            }
    };
};
