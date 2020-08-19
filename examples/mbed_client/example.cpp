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

Mutex mutex;

#define PUTS(...) do{\
    mutex.lock();\
    printf(__VA_ARGS__);\
    printf("\n");\
    fflush(stdout);\
    mutex.unlock();\
}while(0);

int i;
    
int main()
{
    static EthernetInterface eth;
    static WIC::Client<1000,1012> client(eth);

    enum wic_encoding encoding;
    bool fin;
    static char buffer[1000];
    nsapi_size_or_error_t retval;
    nsapi_size_or_error_t bytes;

    int n;
    
    eth.connect();

    for(int i=0; true; i++){

        client.open("ws://192.168.1.108:9001/getCaseCount?agent=mbed");

        retval = client.recv(encoding, fin, buffer, sizeof(buffer));

        if(retval >= 0){

            buffer[retval] = 0;
            n = atoi(buffer);
            PUTS("getCaseCount: %d (#%d)", n, i);
        }
        else{

            PUTS("it errored %d (#$d)", retval, i);
        }

        client.close();

        wait_us(500000);
    }

    for(int tc=1U; tc <= n; tc++){

        const char url_base[] = "ws://192.168.1.108:9001/";
        char url[100];
        snprintf(url, sizeof(url), "%srunCase?case=%d&agent=mbed", url_base, tc);

        retval = client.open(url);

        if(retval == NSAPI_ERROR_OK){

            for(;;){

                retval = client.recv(encoding, fin, buffer, sizeof(buffer));

                if(retval >= 0){

                    if(encoding == WIC_ENCODING_UTF8){

                        PUTS("got text: %.*s", retval, buffer);
                    }

                    bytes = retval;

                    retval = client.send(buffer, bytes, encoding, fin);

                    if(retval != bytes){

                        PUTS("error #send() @ %d retval %d", tc, retval);
                        break;
                    }
                }
                else{

                    PUTS("error #recv() @ %d retval %d", tc, retval);
                    break;
                }
            }

            client.close();    
        }
        else{

            PUTS("error #open() @ %d retval %d", tc, retval);
            break;
        }
    }

    client.open("ws://192.168.1.108:9001/updateReports?agent=mbed");
    client.close();

    for(;;){

        wait_us(2000000);
    }
}
