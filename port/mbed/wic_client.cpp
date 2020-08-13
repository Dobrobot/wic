#include "wic_client.hpp"

using namespace WIC;

/* constructors *******************************************************/

ClientBase::ClientBase(NetworkInterface &interface, BufferBase& rx, InputQueueBase& input, OutputQueueBase& output) :
    interface(interface),
    rx(rx),
    input(input),
    output(output),
    tls(&tcp),
    socket(tcp),
    wakeup(0, 1),
    schema(WIC_SCHEMA_WS)
{                
    writer_thread.start(callback(this, &ClientBase::writer_task));
    reader_thread.start(callback(this, &ClientBase::reader_task));
    event_thread.start(callback(&events, &EventQueue::dispatch_forever));
}

/* static protected ***************************************************/

ClientBase *
ClientBase::to_obj(struct wic_inst *self)
{       
    return static_cast<ClientBase *>(wic_get_app(self));
}

void
ClientBase::handle_send(struct wic_inst *self, const void *data, size_t size, enum wic_frame_type type)
{
    ClientBase *obj = to_obj(self);

    if(obj->tx){

        obj->tx->size = size;
        obj->output.put(&obj->tx);
    }                
}

void *
ClientBase::handle_buffer(struct wic_inst *self, size_t size, enum wic_frame_type type, size_t *max)
{
    void *retval = NULL;
    ClientBase *obj = to_obj(self);

    obj->tx = obj->output.alloc(type, size);

    if(obj->tx){

        *max = obj->tx->max;
        retval = obj->tx->data;
    }

    return retval;
}

uint32_t
ClientBase::handle_rand(struct wic_inst *self)
{
    return rand();
}

void
ClientBase::handle_handshake_failure(struct wic_inst *self, enum wic_handshake_failure reason)
{
    ClientBase *obj = to_obj(self);

    obj->events.cancel(obj->timeout_id);

    obj->job.status = WIC_STATUS_TIMEOUT;
    obj->job.done = true;
    obj->wakeup.release();
}

void
ClientBase::handle_open(struct wic_inst *self)
{
    ClientBase *obj = to_obj(self);

    obj->events.cancel(obj->timeout_id);
    
    if(obj->on_open_cb){

        obj->on_open_cb();
    }

    obj->job.status = WIC_STATUS_SUCCESS;
    obj->job.done = true;
    obj->wakeup.release();
}

void
ClientBase::handle_text(struct wic_inst *self, bool fin, const char *data, uint16_t size)
{
    ClientBase *obj = to_obj(self);
    
    if(obj->on_text_cb){

        obj->on_text_cb(fin, data, size);
    }    
}

void
ClientBase::handle_binary(struct wic_inst *self, bool fin, const void *data, uint16_t size)
{
    ClientBase *obj = to_obj(self);
    
    if(obj->on_binary_cb){

        obj->on_binary_cb(fin, data, size);
    }   
}

void
ClientBase::handle_close(struct wic_inst *self, uint16_t code, const char *reason, uint16_t size)
{
    ClientBase *obj = to_obj(self);

    if(obj->on_close_cb){

        obj->on_close_cb(code, reason, size);
    }
}

void
ClientBase::handle_close_transport(struct wic_inst *self)
{
    to_obj(self)->socket.close();
}

/* protected **********************************************************/

void
ClientBase::do_parse()
{
    BufferBase *buf = input.get();

    if(buf){

        wic_parse(&inst, buf->data, buf->size);
        input.free(&buf);
    }
}

void
ClientBase::do_open(const char *url)
{
    SocketAddress a;
    nsapi_error_t err;

    struct wic_init_arg init_arg = {0};

    /* already open */
    if(state == OPEN){

        job.status = WIC_STATUS_BAD_STATE;
        job.done = true;
        wakeup.release();
        return;
    }

    init_arg.app = this;
    
    init_arg.rx = rx.data;
    init_arg.rx_max = rx.max;

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

        job.status = WIC_STATUS_TIMEOUT;
        job.done = true;
        wakeup.release();
        return;
    }

    schema = wic_get_url_schema(&inst);

    err = interface.gethostbyname(wic_get_url_hostname(&inst), &a);

    if(err != NSAPI_ERROR_OK){

        job.status = WIC_STATUS_TIMEOUT;
        job.done = true;
        wakeup.release();
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
        job.status = WIC_STATUS_TIMEOUT;
        job.done = true;
        wakeup.release();
        return;
    }

    if(wic_start(&inst) != WIC_STATUS_SUCCESS){

        job.status = WIC_STATUS_TIMEOUT;
        job.done = true;
        wakeup.release();
        return;
    }

    flags.set(socket_open_flag);
    
    timeout_id = events.call_in(5000, callback(this, &ClientBase::do_handshake_timeout));        
}

void
ClientBase::do_close()
{
    wic_close(&inst);    
}

void
ClientBase::do_send_text(bool fin, const char *value, uint16_t size)
{
    wic_send_text(&inst, fin, value, size);
}

void
ClientBase::do_send_binary(bool fin, const void *value, uint16_t size)
{
    wic_send_binary(&inst, fin, value, size);
}

void
ClientBase::do_transport_error()
{
    wic_close_with_reason(&inst, WIC_CLOSE_ABNORMAL_2, NULL, 0U);
}

void
ClientBase::do_handshake_timeout()
{
    wic_close_with_reason(&inst, WIC_CLOSE_ABNORMAL_1, NULL, 0U);
}

void
ClientBase::writer_task()
{
    BufferBase *buf;
    nsapi_size_or_error_t ret;
    
    for(;;){

        flags.wait_any(socket_open_flag, osWaitForever, false);

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
                wakeup.release();
            
                if(ret < 0){

                    events.call(callback(this, &ClientBase::do_transport_error));
                    break;
                }                            
            }
        }

        flags.clear(socket_open_flag);
    }
}

void
ClientBase::reader_task()
{
    nsapi_size_or_error_t retval;
    BufferBase *buf;
    
    for(;;){

        flags.wait_any(socket_open_flag, osWaitForever, false);

        for(;;){

            buf = input.alloc();

            retval = socket.recv(buf->data, buf->max);

            if(retval < 0){

                events.call(callback(this, &ClientBase::do_transport_error));
                input.free(&buf);    
                break;
            }
            else{
            
                buf->size = retval;

                input.put(&buf);
                
                events.call(this, &ClientBase::do_parse);
            }
        }

        flags.clear(socket_open_flag);
    }
}

/* public *************************************************************/

enum wic_status
ClientBase::open(const char *url)
{
    enum wic_status retval;

    mutex.lock();

    job = {0};

    events.call(callback(this, &ClientBase::do_open), url);

    while(!job.done){

        wakeup.acquire();
    }

    retval = job.status;

    mutex.unlock();
    
    return retval;
}

void
ClientBase::close()
{
    mutex.lock();

    job = {0};

    events.call(callback(this, &ClientBase::do_close));

    while(!job.done){

        wakeup.acquire();
    }

    mutex.unlock();
}

enum wic_status
ClientBase::text(const char *value)
{
    return text(true, value);
}

enum wic_status
ClientBase::text(const char *value, uint16_t size)
{
    return text(true, value, size);
}

enum wic_status
ClientBase::text(bool fin, const char *value)
{
    enum wic_status retval = WIC_STATUS_SUCCESS;
    int size = strlen(value);

    if(size >= 0){

        retval = text(fin, value, (uint16_t)size);
    }

    return retval;
}

enum wic_status
ClientBase::text(bool fin, const char *value, uint16_t size)
{
    enum wic_status retval;

    mutex.lock();

    for(;;){

        job = {0};

        events.call(callback(this, &ClientBase::do_send_text), fin, value, size);

        while(!job.done){

            wakeup.acquire();
        }

        retval = job.status;

        if(job.status == WIC_STATUS_WOULD_BLOCK){

            wakeup.acquire();
        }
        else{

            break;
        }
    }
     
    mutex.unlock();

    return retval;
}

enum wic_status
ClientBase::binary(const void *value, uint16_t size)
{
    return binary(true, value, size);
}

enum wic_status
ClientBase::binary(bool fin, const void *value, uint16_t size)
{
    enum wic_status retval;

    mutex.lock();

    for(;;){

        job = {0};

        events.call(callback(this, &ClientBase::do_send_binary), fin, value, size);

        while(!job.done){

            wakeup.acquire();
        }

        retval = job.status;

        if(job.status == WIC_STATUS_WOULD_BLOCK){

            wakeup.acquire();
        }
        else{

            break;
        }
    }

    mutex.unlock();

    return retval;
}

bool
ClientBase::is_open()
{
    return state == OPEN;
}

void
ClientBase::on_text(Callback<void(bool,const char *, uint16_t)> handler)
{
    on_text_cb = handler;
}

void
ClientBase::on_binary(Callback<void(bool,const void *, uint16_t)> handler)
{
    on_binary_cb = handler;
}

void
ClientBase::on_open(Callback<void()> handler)
{
    on_open_cb = handler;
}

void
ClientBase::on_close(Callback<void(uint16_t, const char *, uint16_t)> handler)
{
    on_close_cb = handler;
}

nsapi_error_t
ClientBase::set_root_ca_cert(const void *root_ca, size_t len)
{
    return tls.set_root_ca_cert(root_ca, len);
}

nsapi_error_t
ClientBase::set_root_ca_cert(const char *root_ca_pem)
{
    return tls.set_root_ca_cert(root_ca_pem);
}

nsapi_error_t
ClientBase::set_client_cert_key(const char *client_cert_pem, const char *client_private_key_pem)
{
    return tls.set_client_cert_key(client_cert_pem, client_private_key_pem);
}

nsapi_error_t
ClientBase::set_client_cert_key(const void *client_cert_pem, size_t client_cert_len, const void *client_private_key_pem, size_t client_private_key_len)
{
    return tls.set_client_cert_key(client_cert_pem, client_cert_len, client_private_key_pem, client_private_key_len);
}
