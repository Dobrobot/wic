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

#include "wic_client.hpp"
#include "EthernetInterface.h"

#define PUTS(...) do{\
    printf(__VA_ARGS__);\
    printf("\n");\
    fflush(stdout);\
}while(0);
    

void on_text(bool fin, const char *value, uint16_t size)
{
    PUTS("got text: %.*s", size, value);
}

void on_binary(bool fin, const void *value, uint16_t size)
{
    PUTS("got binary: <not shown>");
}

void on_open(void)
{
    PUTS("websocket is open!");
}

void on_close(uint16_t code, const char *reason, uint16_t size)
{
    PUTS("websocket closed (%u)", code);
}

int main()
{
    //printf("starting!\n");

    static EthernetInterface eth;

    eth.connect();

    static WIC::Client<1000,1012> client(eth);

    client.on_text(callback(on_text));
    client.on_binary(callback(on_binary));
    client.on_open(callback(on_open));
    client.on_close(callback(on_close));

    client.open("ws://192.168.1.108:9001/getCaseCount");

    for(int i=0;;i++){

        //printf("hey %d\n", i);
        wait_us(2000000);
    }
}
