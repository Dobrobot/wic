WebSockets in C
===============

WIC is a C99 implementation of [rfc6455](https://tools.ietf.org/html/rfc6455)
websockets designed for embedded applications.

WIC decouples the websocket protocol from the transport layer. This makes
it possible to use WIC over any transport layer assuming that you are prepared
to do the integration work.

WIC is a work in progress. This means that:

- server role is not supported yet
- handshake implementation is not robust and is only exercised on the
  happy path
- interfaces are not stable

## Features

- doesn't call malloc
- handshake header fields passed through to application
- convenience functions for dissecting URLs
- convenience functions for implementing redirection
- works with any transport layer you like
- automatic payload fragmentation on receive
- trivial to integrate with an existing build system

## Limitations

- interfaces are not thread-safe (provide your own solution)
- handshake headers are only accessible at the moment a websocket
  becomes connected (i.e. wic_get_state() == WIC_STATE_READY)
- doesn't support extensions
- there are a bewildering number of function pointers

The handshake field limitation is a consequence of storing header
fields in the same buffer as used for receiving websocket frames. Applications
that require header fields to persist beyond WIC_STATE_READY will need
to copy the fields when they are available.

## Integrations

- [mbed wrapper](port/mbed)

## Compiling

- add `include` to the search path
- compile sources in `src`

There are some macros you can define. These listed at the top of [include/wic.h](include/wic.h) and
in the [API documentation](https://cjhdev.github.io/wic_api/).

The WIC_PORT_HEADER macro can be used to define a filename which the
will include into wic.h. This might be the most
convenient place to define/redefine other macros.

## Usage

Below is an example of a client that:

- connects
- sends a "hello world" text
- closes normally

~~~ c
#include "wic.h"

int main(int argc, char **argv)
{
    int s;
    static uint8_t rx[1000];
    struct wic_inst inst;
    struct wic_init_arg arg = {0};

    arg.rx = rx; arg.rx_max = sizeof(rx);    

    arg.on_send = on_send_handler;    
    arg.on_open = on_open_handler;        
    arg.on_message = on_message_handler;        
    arg.on_close_transport = on_close_transport_handler;        
    arg.on_buffer = on_buffer_handler;        

    arg.app = &s;
    arg.url = "ws://echo.websocket.org/";
    arg.role = WIC_ROLE_CLIENT;

    if(!wic_init(&inst, &arg)){

        exit(EXIT_FAILURE);
    };

    if(transport_open_client(wic_get_url_schema(&inst),
            wic_get_url_hostname(&inst), wic_get_url_port(&inst), &s)){

        if(wic_start(&inst) == WIC_STATUS_SUCCESS){

            while(transport_recv(s, &inst));
        }
        else{

            transport_close(&s);
        }
    }
    
    exit(EXIT_SUCCESS);
}

static void on_open_handler(struct wic_inst *inst)
{
    const char msg[] = "hello world";

    wic_send_text(inst, true, msg, strlen(msg));    
} 

static void on_close_transport_handler(struct wic_inst *inst)
{
    transport_close((int *)wic_get_app(inst));
}

static void on_send_handler(struct wic_inst *inst, const void *data,
        size_t size, enum wic_frame_type type)
{
    transport_write(*(int *)wic_get_app(inst), data, size);
}

static void *on_buffer_handler(struct wic_inst *inst, size_t min_size,
        enum wic_frame_type type, size_t *max_size)
{
    static uint8_t tx[1000U];

    *max_size = sizeof(tx);

    return (min_size <= sizeof(tx)) ? tx : NULL;
}

static bool on_message_handler(struct wic_inst *inst, enum wic_encoding encoding, bool fin, const char *data, uint16_t size)
{
    if(encoding == WIC_ENCODING_UTF8){

        printf("received text: %.*s\n", size, data);
    }

    wic_close(inst);

    return true;
}
~~~

## Acknowledgements

WIC integrates code from the following projects:

- Joyent/Node-JS http-parser
- Bjoern Hoehrmann's UTF8 parser
- MBED TLS SHA1

The Autobahn Test Suite has been used for verification.





