#include "wic_client.hpp"

using namespace WIC;

/* constructors *******************************************************/

ClientBase::ClientBase(NetworkInterface &interface, BufferBase& rx, InputPoolBase& input, OutputQueueBase& output, BufferBase& url) :
    interface(interface),
    rx(rx),
    input(input),
    output(output),
    url(url),
    tls(&tcp),
    socket(tcp),
    condition(mutex),
    events(100 * EVENTS_EVENT_SIZE)
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
        obj->output.put(obj->tx);
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

    obj->job.handshake_failure_reason = reason;

    switch(reason){
    default:
    /* no response within timeout (either socket or message timeout) */
    case WIC_HANDSHAKE_FAILURE_ABNORMAL_1:
        //obj->job.retval = NSAPI_ERROR_;
        break;
        
    /* socket closed / transport errored */
    case WIC_HANDSHAKE_FAILURE_ABNORMAL_2:
        //obj->job.retval = ;
        break;
        
    /* response was not HTTP */
    case WIC_HANDSHAKE_FAILURE_PROTOCOL:
        //obj->job.retval = ;
        break;
        
    /* impossible */
    case WIC_HANDSHAKE_FAILURE_TLS:
        break;
        
    case WIC_HANDSHAKE_FAILURE_IRRELEVANT:
        break;

    /* connection was not upgraded */
    case WIC_HANDSHAKE_FAILURE_UPGRADE:
        obj->job.retval = NSAPI_ERROR_OK;
        break;
    }

    obj->job.done = true;
    obj->notify();
}

void
ClientBase::handle_open(struct wic_inst *self)
{
    ClientBase *obj = to_obj(self);

    obj->events.cancel(obj->timeout_id);

    obj->state = OPEN;
    
    if(obj->on_open_cb){

        obj->on_open_cb();
    }

    obj->job.retval = NSAPI_ERROR_OK;
    obj->job.done = true;
    obj->notify();
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

    obj->state = CLOSED;

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
ClientBase::do_parse(BufferBase *buf)
{
    wic_parse(&inst, buf->data, buf->size);
    input.free(buf);
}

void
ClientBase::do_open()
{
    SocketAddress a;
    nsapi_error_t err;

    struct wic_init_arg init_arg = {0};

    /* already open */
    if(state == OPEN){

        job.retval = NSAPI_ERROR_IS_CONNECTED;
        job.done = true;
        notify();
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
    
    init_arg.url = (const char *)url.data;
    
    if(!wic_init(&inst, &init_arg)){

        job.retval = NSAPI_ERROR_PARAMETER;
        job.done = true;
        notify();
        return;
    }

    err = interface.gethostbyname(wic_get_url_hostname(&inst), &a);

    if(err != NSAPI_ERROR_OK){
        
        job.retval = err;
        job.done = true;
        notify();
        return;
    }

    a.set_port(wic_get_url_port(&inst));

    switch(wic_get_url_schema(&inst)){
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

    (void)tcp.open(&interface);

    err = socket.connect(a);

    if(err != NSAPI_ERROR_OK){

        socket.close();
        job.retval = err;
        job.done = true;
        notify();
        return;
    }

    if(wic_start(&inst) != WIC_STATUS_SUCCESS){

        //job.retval = ;
        job.done = true;
        notify();
        return;
    }

    flags.set(start_reader_flag | start_writer_flag);

    timeout_id = events.call_in(5000, callback(this, &ClientBase::do_handshake_timeout));        
}

void
ClientBase::do_close()
{
    wic_close(&inst);

    //wait for reader/writer thread to park

    job.done = true;
    job.retval = NSAPI_ERROR_OK;
    notify();
}

void
ClientBase::do_send_text(bool fin, const char *value, uint16_t size)
{
    job.status = wic_send_text(&inst, fin, value, size);
    job.done = true;
    notify();
}

void
ClientBase::do_send_binary(bool fin, const void *value, uint16_t size)
{
    job.status = wic_send_binary(&inst, fin, value, size);
    job.done = true;
    notify();
}

void
ClientBase::do_transport_error(nsapi_error_t status)
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
    nsapi_size_or_error_t retval;
    size_t pos;
    
    for(;;){

        flags.wait_any(start_writer_flag);

        for(;;){

            buf = output.get();

            if(buf){

                pos = 0U;

                do{

                    retval = socket.send(&buf->data[pos], buf->size - pos);

                    if(retval >= 0){

                        pos += retval;
                    }
                }
                while((retval >= 0) && (pos < buf->size));

                /* if this buffer contains a close, ensure output queue is clear */
                if(buf->priority == 2){

                    output.free(buf);                    
                    notify();
                    socket.close();
                    break;
                }
                else{

                    output.free(buf);
                    notify();
            
                    if(retval < 0){

                        events.call(callback(this, &ClientBase::do_transport_error), retval);
                        break;
                    }
                }
            }
        }

        output.clear();
    }
}

void
ClientBase::reader_task()
{
    nsapi_size_or_error_t retval;
    BufferBase *buf;
    
    for(;;){

        flags.wait_any(start_reader_flag);

        for(;;){

            buf = input.alloc();

            retval = socket.recv(buf->data, buf->max);

            if(retval < 0){

                events.call(callback(this, &ClientBase::do_transport_error), retval);
                input.free(buf);    
                break;
            }
            else{
            
                buf->size = retval;
                events.call(callback(this, &ClientBase::do_parse), buf);
            }
        }

        flags.clear(reader_on_flag);
    }
}

/* public *************************************************************/

nsapi_error_t
ClientBase::open(const char *url)
{
    uint32_t n = max_redirects;
    nsapi_error_t retval = NSAPI_ERROR_PARAMETER;
    const char *url_ptr = url;

    mutex.lock();

    for(;;){

        job = {};

        /* URLs cannot be larger than what we can
         * buffer.
         *
         * We buffer so as to support
         * redirects */
        if(strlen(url_ptr) >= this->url.max){

            break;
        }

        this->url.size = strlen(url_ptr)+1U;
        strcpy((char *)this->url.data, url_ptr);
        
        events.call(callback(this, &ClientBase::do_open));

        while(!job.done){

            condition.wait();
        }

        if(
            (job.handshake_failure_reason == WIC_HANDSHAKE_FAILURE_UPGRADE)
            &&
            (wic_get_redirect_url(&inst) != NULL)
            &&
            (n > 0U)
        ){

            n--;
            url_ptr = wic_get_redirect_url(&inst);
        }
        else{

            retval = job.retval;
            break;
        }
    }

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

        condition.wait();
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

            condition.wait();
        }

        retval = job.status;

        if(job.status == WIC_STATUS_WOULD_BLOCK){

            condition.wait();
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

            condition.wait();
        }

        retval = job.status;

        if(job.status == WIC_STATUS_WOULD_BLOCK){

            condition.wait();
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
