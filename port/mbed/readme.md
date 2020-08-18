MBED WIC Wrapper
================

This wrapper makes it easy to use WIC on MBED.

## WIC::Client<size_t RX_MAX, size_t TX_MAX>

A wrapper for client mode.

- TX and RX buffer size defined by template
- blocking interfaces that behave like the regular socket classes
- TCP or TLS mode determined by URL
- prioritised output buffer queue
- no need to create/destroy for each connection
- subclass of WIC::ClientBase for generic handling of multiple connections

An example:

~~~ c++
#include "wic_client.hpp"
#include "EthernetInterface.h"

static EthernetInterface eth;
static WIC::Client<1000, 1012> client(eth);

void on_text()
{
    printf("on_text: %.*s\n", size, data);
}

int main()
{
    enum wic_encoding encoding;
    bool fin;
    static char buffer[1000];
    nsapi_size_or_error_t retval;

    eth.connect();

    for(;;){

        if(client.open("wss://echo.websocket.org/") == NSAPI_ERROR_OK){

            client.send("hello world!");

            retval = client.recv(encoding, fin, buffer, 100);

            if((retval > 0) && (encoding == WIC_ENCODING_UTF8)){

                printf("got: %.*s\n", retval, buffer);
            }
            
            client.close();        
        }

        ThisThread::sleep_for(10);        
    }
}
~~~

## WIC::Server

No support for this mode at this time.

## Installation

Clone the WIC repository into a directory in your MBED project.
The `.mbedignore` file is written in such a way that the right sources
will be found by the build system.

## License

MIT

