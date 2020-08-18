#include "wic_client.hpp"

#include <assert.h>

using namespace WIC;

/* constructors *******************************************************/

ClientBase::ClientBase(NetworkInterface &interface, UserQueueBase& user_queue, InputPoolBase& rx_pool, OutputQueueBase& tx_queue, BufferBase& url) :
    interface(interface),
    user_queue(user_queue),
    rx_pool(rx_pool),
    tx_queue(tx_queue),    
    url(url),
    tls(&tcp),
    socket(tcp),
    condition(mutex),
    events(100 * EVENTS_EVENT_SIZE)
{                
    rx = user_queue.alloc();
    socket.sigio(callback(this, &ClientBase::do_sigio));
    worker_thread.start(callback(this, &ClientBase::worker_task));
    ticker.attach_us(callback(this, &ClientBase::do_tick), 1000000UL);
}

/* static protected ***************************************************/

ClientBase *
ClientBase::to_obj(struct wic_inst *self)
{       
    return static_cast<ClientBase *>(wic_get_app(self));
}

void
ClientBase::handle_send(struct wic_inst *self, const void *data, size_t size, enum wic_buffer type)
{
    ClientBase *obj = to_obj(self);

    if(obj->tx){

        obj->tx->size = size;
        obj->tx_queue.put(&obj->tx);
    }                
}

void *
ClientBase::handle_buffer(struct wic_inst *self, size_t size, enum wic_buffer type, size_t *max)
{
    void *retval = NULL;
    ClientBase *obj = to_obj(self);

    obj->tx = obj->tx_queue.alloc(type, size, max);

    if(obj->tx){

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
        obj->job.retval = NSAPI_ERROR_CONNECTION_TIMEOUT;
        break;
        
    /* socket closed / transport errored */
    case WIC_HANDSHAKE_FAILURE_ABNORMAL_2:
        obj->job.retval = NSAPI_ERROR_CONNECTION_LOST;
        break;
        
    /* response was not HTTP */
    case WIC_HANDSHAKE_FAILURE_PROTOCOL:
        obj->job.retval = NSAPI_ERROR_UNSUPPORTED;
        break;
    
    /* connection was not upgraded */
    case WIC_HANDSHAKE_FAILURE_UPGRADE:
        obj->job.retval = NSAPI_ERROR_UNSUPPORTED;
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

bool
ClientBase::handle_message(struct wic_inst *self, enum wic_encoding encoding, bool fin, const char *data, uint16_t size)
{
    bool retval = false;
    ClientBase *obj = to_obj(self);
    BufferBase *ptr = obj->user_queue.alloc();

    if(ptr){

        /* the rx buffer inside wic_inst is from the user_queue memory
         * pool so overrun is impossible */

        ptr->encoding = encoding;
        ptr->fin = fin;
        ptr->size = size;
        (void)memcpy(ptr->data, data, size);
        
        obj->user_queue.put(&ptr);

        retval = true;
    }

    return retval;
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
    /* this ensures that the output queue is flushed */
    to_obj(self)->tx_queue.put(nullptr);        
}

void
ClientBase::handle_ping(struct wic_inst *self)
{
    //ClientBase *obj = to_obj(self);
}

void
ClientBase::handle_pong(struct wic_inst *self)
{
    //ClientBase *obj = to_obj(self);
}

/* protected **********************************************************/

void
ClientBase::do_work()
{
    work.release();
}

void
ClientBase::do_close_socket()
{
    socket.close();            
    job.done = true;
    notify();
}

void
ClientBase::do_open()
{
    SocketAddress a;
    nsapi_error_t err;

    struct wic_init_arg init_arg = {0};

    if(state != CLOSED){

        switch(state){
        default:
        case OPENING:
        case CLOSING:
            job.retval = NSAPI_ERROR_BUSY;
            break;
        case OPEN:
            job.retval = NSAPI_ERROR_IS_CONNECTED;
            break;
        }

        job.done = true;
        notify();
        return;
    }

    init_arg.app = this;
    
    init_arg.rx = rx->data;
    init_arg.rx_max = rx->max;

    init_arg.on_open = handle_open;
    init_arg.on_close = handle_close;
    init_arg.on_message = handle_message;
    init_arg.on_close_transport = handle_close_transport;
    init_arg.on_handshake_failure = handle_handshake_failure;

    init_arg.on_send = handle_send;
    init_arg.on_buffer = handle_buffer;
    init_arg.rand = handle_rand;
    
    init_arg.role = WIC_ROLE_CLIENT;
    
    init_arg.url = (const char *)url.data;
    
    if(!wic_init(&inst, &init_arg)){

        job.retval = NSAPI_ERROR_PARAMETER;
        do_close_socket();
        return;
    }

    err = interface.gethostbyname(wic_get_url_hostname(&inst), &a);

    if(err != NSAPI_ERROR_OK){
        
        job.retval = err;
        do_close_socket();
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

        job.retval = err;
        do_close_socket();
        return;
    }

    socket.set_blocking(false);

    job.status = wic_start(&inst);

    if(job.status != WIC_STATUS_SUCCESS){

        job.retval = NSAPI_ERROR_OK;
        do_close_socket();
        return;
    }

    state = OPENING;

    timeout_id = events.call_in(5000, callback(this, &ClientBase::do_handshake_timeout));   
}

void
ClientBase::do_close()
{
    switch(wic_get_state(&inst)){
    case WIC_STATE_READY:
    case WIC_STATE_OPEN:
        wic_close(&inst);
        break;
    case WIC_STATE_INIT:
    case WIC_STATE_CLOSED:
    default:
        job.retval = NSAPI_ERROR_OK;
        job.done = true;
        notify();
        break;
    }    
}

void
ClientBase::do_send(enum wic_encoding encoding, bool fin, const char *value, uint16_t size)
{
    job.status = wic_send(&inst, encoding, fin, value, size);
    job.done = true;
    notify();
}

void
ClientBase::do_handshake_timeout()
{
    wic_close_with_reason(&inst, WIC_CLOSE_ABNORMAL_1, NULL, 0U);
}

void
ClientBase::worker_task()
{
    nsapi_size_or_error_t retval;
    size_t tx_pos = 0U;
    size_t rx_pos = 0U;
    BufferBase *_rx = nullptr;
    BufferBase *_tx = nullptr;

    for(;;){

        work.acquire();

        events.dispatch(0);

        /* try to get an RX buffer */
        if(!_rx){

            _rx = rx_pool.alloc();
            rx_pos = 0U;
        }

        /* try to read the socket if there is an RX buffer */
        if(_rx && (_rx->size == 0U)){

            retval = socket.recv(_rx->data, _rx->max);

            if(retval > 0){

                _rx->size = retval;
            }
            else{

                switch(retval){
                case NSAPI_ERROR_WOULD_BLOCK:
                case NSAPI_ERROR_NO_SOCKET:
                    break;
                default:

                    wic_close_with_reason(&inst, WIC_CLOSE_ABNORMAL_2, NULL, 0U);
                    do_work();                    
                    rx_pool.free(&_rx);
                }
            }            
        }

        /* try to parse the data read from socket */
        if(_rx && (_rx->size > 0U)){
            
            size_t bytes = wic_parse(&inst, &_rx->data[rx_pos], _rx->size - rx_pos);

            rx_pos += bytes;

            if(rx_pos == _rx->size){

                rx_pool.free(&_rx);
            }
        }

        /* try to get a TX buffer */
        if(!_tx){

            _tx = tx_queue.get();
            tx_pos = 0U;
        }

        /* try to write the TX buffer to the socket */
        if(_tx){

            retval = socket.send(&_tx->data[tx_pos], _tx->size - tx_pos);

            if(retval >= 0){

                tx_pos += retval;

                if(tx_pos == _tx->size){

                    tx_queue.free(&_tx);
                    notify();
                }
            }
            else{

                switch(retval){
                case NSAPI_ERROR_WOULD_BLOCK:
                case NSAPI_ERROR_NO_SOCKET:
                    break;
                default:

                    wic_close_with_reason(&inst, WIC_CLOSE_ABNORMAL_2, NULL, 0U);
                    do_work();
                    tx_queue.free(&_tx);
                    notify();
                    break;
                }                    
            }
        }    
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
        do_work();

        while(!job.done){

            wait();
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
    do_work();

    while(!job.done){

        wait();
    }

    mutex.unlock();
}

nsapi_size_or_error_t
ClientBase::send(const char *data, uint16_t size, enum wic_encoding encoding, bool fin)
{
    nsapi_size_or_error_t retval = NSAPI_ERROR_PARAMETER;

    mutex.lock();

    for(;;){

        job = {0};

        events.call(callback(this, &ClientBase::do_send), encoding, fin, data, size);

        do_work();

        while(!job.done){

            wait();
        }

        if(job.status == WIC_STATUS_WOULD_BLOCK){

            wait();
        }
        else{

            switch(job.status){
            case WIC_STATUS_SUCCESS:
                retval = size;
                break;
            default:
                retval = NSAPI_ERROR_PARAMETER;
                break;            
            }

            break;
        }
    }

    mutex.unlock();

    return retval;
}

nsapi_size_or_error_t
ClientBase::recv(enum wic_encoding& encoding, bool &fin, char *buffer, uint32_t timeout)
{
    nsapi_size_or_error_t retval = NSAPI_ERROR_WOULD_BLOCK;
    BufferBase *ptr;
    osEvent evt;

    evt = user_queue.get(timeout);

    switch(evt.status){
    case osEventMessage:

        if(evt.value.p == NULL){

            retval = NSAPI_ERROR_NO_SOCKET;
        }
        else{

            ptr = static_cast<BufferBase *>(evt.value.p);

            encoding = ptr->encoding;
            fin = ptr->fin;
            (void)memcpy(buffer, ptr->data, ptr->size);
            retval = ptr->size;

            user_queue.free(&ptr);
        }

        do_work();
        break;
    
    case osEventTimeout:
        retval = NSAPI_ERROR_WOULD_BLOCK;
        break;
    case osOK:
    case osErrorParameter:
    default:
        retval = NSAPI_ERROR_PARAMETER;
        break;
    }
    
    return retval;
}

bool
ClientBase::is_open()
{
    return state == OPEN;
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
