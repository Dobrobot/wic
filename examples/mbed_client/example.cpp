#include "wic_client.hpp"
#include "EthernetInterface.h"

void on_text(bool fin, const char *value, uint16_t size)
{
    printf("got text: %.*s\n", size, value);
}

void on_binary(bool fin, const void *value, uint16_t size)
{
    printf("got binary: <not shown>\n");
}

void on_open(void)
{
    printf("websocket is open!\n");
}

void on_close(uint16_t code, const char *reason, uint16_t size)
{
    printf("websocket closed (%u)", code);
}

int main()
{
    static EthernetInterface eth;

    eth.connect();

    static WICClient<1000,1012> client(eth);

    client.on_text(callback(on_text));
    client.on_binary(callback(on_binary));
    client.on_open(callback(on_open));
    client.on_close(callback(on_close));
    
    for(;;){

        if(client.open("wss://echo.websocket.org/")){

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
