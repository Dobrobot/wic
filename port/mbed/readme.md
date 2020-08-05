MBED WIC Wrapper
================

This wrapper makes it a breeze to use WIC on MBED.

## WICClient

This is a wrapper class for client mode. Features/characteristics include:

- blocking interfaces
- TCP and TLS modes (determined by URL)
- responds to socket 'back-pressure'
- long life object, no need to create/destroy for each connection

An example:

~~~ c++
#include "wic_client.hpp"
#include "EthernetInterface.h"

int main()
{
    static EthernetInterface eth;

    eth.connect();

    static WICClient<1000, 1008> client(eth);

    for(;;){

        if(client.open("ws://echo.websocket.org/")){

            while(client.is_open()){

                client.text("hello world!");
                wait_us(5000);
            }        
        }
        else{

            wait_us(10000);
        }
    }
}
~~~

## WICServer

No support for this mode at this time.

## Installation

Clone the WIC repository into a directory in your MBED project.
The `.mbedignore` file is written in such a way that the right sources
will be found by the build system.

## License

MIT

