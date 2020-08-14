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

#ifndef WIC_CLIENT_HPP
#define WIC_CLIENT_HPP

#include "mbed.h"
#include "wic.h"
#include "TLSSocket.h"
#include "wic_output_queue.hpp"
#include "wic_input_pool.hpp"
#include "wic_buffer.hpp"

namespace WIC {

    class ClientBase {

        protected:

            enum State {

                CLOSED,
                OPEN

            } state;

            static const uint32_t start_reader_flag = 1U;
            static const uint32_t start_writer_flag = 2U;
            static const uint32_t reader_on_flag = 4U;
            static const uint32_t writer_on_flag = 8U;

            NetworkInterface &interface;

            BufferBase& rx;            
            InputPoolBase& input;
            OutputQueueBase& output;
            BufferBase& url;
            
            TCPSocket tcp;
            TLSSocketWrapper tls;
            Socket &socket;            
            Mutex mutex;
            ConditionVariable condition;
            EventQueue events;
            EventFlags flags;

            static const uint32_t max_redirects = 3U;

            struct Job {

                bool done;
                nsapi_error_t retval;
                enum wic_status status;
                enum wic_handshake_failure handshake_failure_reason;
            };

            int timeout_id;
            BufferBase *tx;
            Job job;

            Thread writer_thread;        
            Thread reader_thread;
            Thread event_thread;

            struct wic_inst inst;

            Callback<void(bool,const char *, uint16_t)> on_text_cb;
            Callback<void(bool,const void *, uint16_t)> on_binary_cb;
            Callback<void()> on_open_cb;
            Callback<void(uint16_t, const char *, uint16_t)> on_close_cb;

            static ClientBase *to_obj(struct wic_inst *self);
            static void handle_send(struct wic_inst *self, const void *data, size_t size, enum wic_frame_type type);
            static void *handle_buffer(struct wic_inst *self, size_t size, enum wic_frame_type type, size_t *max);
            static uint32_t handle_rand(struct wic_inst *self);
            static void handle_handshake_failure(struct wic_inst *self, enum wic_handshake_failure reason);
            static void handle_open(struct wic_inst *self);
            static void handle_text(struct wic_inst *self, bool fin, const char *data, uint16_t size);
            static void handle_binary(struct wic_inst *self, bool fin, const void *data, uint16_t size);
            static void handle_close(struct wic_inst *self, uint16_t code, const char *reason, uint16_t size);
            static void handle_close_transport(struct wic_inst *self);

            /* these are requested via public methods */
            void do_open();
            void do_close();
            void do_send_text(bool fin, const char *value, uint16_t size);
            void do_send_binary(bool fin, const void *value, uint16_t size);

            void do_parse(BufferBase *buf);            
            void do_transport_error(nsapi_error_t status);
            void do_handshake_timeout();

            void writer_task();
            void reader_task();

            void notify()
            {
                mutex.lock();
                condition.notify_one();
                mutex.unlock();                
            }

        public:

            ClientBase(NetworkInterface &interface, BufferBase &rx, InputPoolBase &input, OutputQueueBase &output, BufferBase &url);

            /** Open the websocket
             *
             * @param[in] url
             *
             * @return nsapi_error_t
             *
             * open will return one of the following codes:
             * 
             * @retval NSAPI_ERROR_OK                   success
             * @retval NSAPI_ERROR_PARAMETER            badly formatted URL
             * @retval NSAPI_ERROR_IS_CONNECTED         already open
             * @retval NSAPI_ERROR_CONNECTION_LOST      socket closed unexpectedly
             * @retval NSAPI_ERROR_CONNECTION_TIMEOUT   socket timeout
             *
             * */
            nsapi_error_t open(const char *url);

            /* close an open websocket */
            void close();

            //bool set_header(String key, String value);
            //bool get_header(String key, String &value);
            
            //bool enable_ping(uint32_t interval, uint32_t response_time);
            //bool disable_ping();

            /* send UTF8 text
             *
             * these return wic_status since NSAPI is too generic to
             * distinguish between "too large to send event"
             *  */
            enum wic_status text(const char *value);
            enum wic_status text(const char *value, uint16_t size);
            enum wic_status text(bool fin, const char *value);
            enum wic_status text(bool fin, const char *value, uint16_t size);

            /* send binary */
            enum wic_status binary(const void *value, uint16_t size);
            enum wic_status binary(bool fin, const void *value, uint16_t size);

            /* is websocket open? */
            bool is_open();

            /* set callbacks */
            void on_text(Callback<void(bool,const char *, uint16_t)> handler);            
            void on_binary(Callback<void(bool,const void *, uint16_t)> handler);
            void on_open(Callback<void()> handler);
            void on_close(Callback<void(uint16_t, const char *, uint16_t)> handler);

            /* TLS settings */
            nsapi_error_t set_root_ca_cert(const void *root_ca, size_t len);
            nsapi_error_t set_root_ca_cert(const char *root_ca_pem);
            nsapi_error_t set_client_cert_key(const char *client_cert_pem, const char *client_private_key_pem);
            nsapi_error_t set_client_cert_key(const void *client_cert_pem, size_t client_cert_len, const void *client_private_key_pem, size_t client_private_key_len);            
    };

    template<size_t RX_MAX, size_t TX_MAX, size_t URL_MAX = 200>
    class Client : public ClientBase {

        protected:

            Buffer<RX_MAX> _rx;
            OutputQueue<TX_MAX> _output;
            InputPool<RX_MAX> _input;
            Buffer<URL_MAX> _url;
            
        public:

            Client(NetworkInterface &interface) :
                ClientBase(interface, _rx, _input, _output, _url)
            {};
    };
};

#endif
