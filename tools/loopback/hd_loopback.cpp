/*
    Copyright 2019 (C) Alexey Dynda

    This file is part of Tiny Protocol Library.

    Protocol Library is free software: you can redistribute it and/or modify
    it under the terms of the GNU Lesser General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    Protocol Library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public License
    along with Protocol Library.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "serial_api.h"
#include "proto/half_duplex/tiny_hd.h"
#include "TinyProtocol.h"
#include <stdio.h>
#include <time.h>
#include <chrono>
#include <thread>

enum class protocol_type_t: uint8_t
{
    HDLC = 0,
    HD = 1,
    FD = 2,
};

static hdlc_crc_t s_crc = HDLC_CRC_8;
static char *s_port = nullptr;
static bool s_generatorEnabled = false;
static protocol_type_t s_protocol = protocol_type_t::HD;
static int s_packetSize = 64;
static bool s_terminate = false;

static int serial_send(void *p, const void *buf, int len)
{
    return SerialSend((uintptr_t)p, buf, len);
}

static int serial_receive(void *p, void *buf, int len)
{
    return SerialReceive((uintptr_t)p, buf, len);
}

static void print_help()
{
    fprintf(stderr, "Usage: sperf -p <port> [-c <crc>]\n");
    fprintf(stderr, "Note: communication runs at 115200\n");
    fprintf(stderr, "    -p <port>, --port <port>   com port to use\n");
    fprintf(stderr, "                               COM1, COM2 ...  for Windows\n");
    fprintf(stderr, "                               /dev/ttyS0, /dev/ttyS1 ...  for Linux\n");
    fprintf(stderr, "    -t <proto>, --protocol <proto> toe of protocol to use\n");
    fprintf(stderr, "                               hd - half duplex (default)\n");
    fprintf(stderr, "    -c <crc>, --crc <crc>      crc type: 0, 8, 16, 32\n");
    fprintf(stderr, "    -g, --generator            turn on packet generating\n");
    fprintf(stderr, "    -s, --size                 packet size: 64 (by default)\n");
}

static int parse_args(int argc, char *argv[])
{
    if ( argc < 2)
    {
        return -1;
    }
    int i = 1;
    while (i < argc)
    {
        if ( argv[i][0] != '-' )
        {
            break;
        }
        if ((!strcmp(argv[i],"-p")) || (!strcmp(argv[i],"--port")))
        {
            if (++i < argc ) s_port = argv[i]; else return -1;
        }
        else if ((!strcmp(argv[i],"-c")) || (!strcmp(argv[i],"--crc")))
        {
            if (++i >= argc ) return -1;
            switch (strtoul(argv[i], nullptr, 10) )
            {
                case 0: s_crc = HDLC_CRC_OFF; break;
                case 8: s_crc = HDLC_CRC_8; break;
                case 16: s_crc = HDLC_CRC_16; break;
                case 32: s_crc = HDLC_CRC_32; break;
                default: fprintf(stderr, "CRC type not supported\n"); return -1;
            }
        }
        else if ((!strcmp(argv[i],"-s")) || (!strcmp(argv[i],"--size")))
        {
            if (++i >= argc ) return -1;
            s_packetSize = strtoul(argv[i], nullptr, 10);
            if (s_packetSize < 32 )
            {
                fprintf(stderr, "Packets size less than 32 bytes are not supported\n"); return -1;
                return -1;
            }
        }
        else if ((!strcmp(argv[i],"-g")) || (!strcmp(argv[i],"--generator")))
        {
            s_generatorEnabled = true;
        }
        else if ((!strcmp(argv[i],"-t")) || (!strcmp(argv[i],"--protocol")))
        {
            if (++i >= argc ) return -1;
            if (!strcmp(argv[i], "hd"))
                s_protocol = protocol_type_t::HD;
            else if (!strcmp(argv[i], "fd"))
                s_protocol = protocol_type_t::FD;
            else
                return -1;
        }
        i++;
    }
    if (s_port == nullptr)
    {
        return -1;
    }
    return i;
}

//=================================== HD ============================================

static void onReceiveFrameHd(void *handle, uint16_t uid, uint8_t *pdata, int size)
{
    fprintf(stderr, "<<< Frame received UID=%04X, payload len=%d\n", uid, size);
}

static void onSendFrameHd(void *handle, uint16_t uid, uint8_t *pdata, int size)
{
    fprintf(stderr, ">>> Frame sent UID=%04X, payload len=%d\n", uid, size);
}

static int run_hd(SerialHandle port)
{
    uint8_t inBuffer[s_packetSize * 2]{};
    STinyHdData tiny{};
    STinyHdInit init{};
    init.write_func       = serial_send;
    init.read_func        = serial_receive;
    init.pdata            = (void *)port;
    init.on_frame_cb      = onReceiveFrameHd;
    init.on_sent_cb       = onSendFrameHd;
    init.inbuf            = inBuffer;
    init.inbuf_size       = sizeof(inBuffer);
    init.timeout          = 100;
    init.crc_type         = s_crc;
    init.multithread_mode = 0;
    /* Initialize tiny hd protocol */
    tiny_hd_init( &tiny, &init  );

    /* Run main cycle forever */
    for(;;)
    {
        if (s_generatorEnabled)
        {
            Tiny::PacketD packet(s_packetSize);
            packet.put("Generated frame");
            if ( tiny_send_wait_ack(&tiny, packet.data(), packet.size()) < 0 )
            {
                fprintf( stderr, "Failed to send packet\n" );
            }
        }
        tiny_hd_run( &tiny );
    }
    tiny_hd_close(&tiny);
}

//================================== FD ======================================

SerialHandle s_serialFd;
Tiny::ProtoFdD *s_protoFd = nullptr;

void onReceiveFrameFd(Tiny::IPacket &pkt)
{
    fprintf(stderr, "<<< Frame received payload len=%d\n", (int)pkt.size() );
    if ( !s_generatorEnabled )
    {
        if ( s_protoFd->write( pkt ) < 0 )
        {
            fprintf( stderr, "Failed to send packet\n" );
        }
    }
}

static int serial_send_fd(void *p, const void *buf, int len)
{
    return SerialSend(s_serialFd, buf, len);
}

static int serial_receive_fd(void *p, void *buf, int len)
{
    return SerialReceive(s_serialFd, buf, len);
}

static int run_fd(SerialHandle port)
{
    s_serialFd = port;
    Tiny::ProtoFdD proto( tiny_fd_buffer_size_by_mtu( s_packetSize, 4 ) );
    // Set window size to 4 frames. This should be the same value, used by other size
    proto.setWindowSize( 4 );
    // Set send timeout to 1000ms as we are going to use multithread mode
    // With generator mode it is ok to send with timeout from run_fd() function
    // But in loopback mode (!generator), we must resend frames from receiveCallback as soon as possible, use no timeout then
    proto.setSendTimeout( s_generatorEnabled ? 1000: 0 );
    proto.setReceiveCallback( onReceiveFrameFd );
    s_protoFd = &proto;

    proto.begin( serial_send_fd, serial_receive_fd );
    std::thread rxThread( [](Tiny::ProtoFdD &proto)->void { while (!s_terminate) proto.run_rx(100); }, std::ref(proto) );
    std::thread txThread( [](Tiny::ProtoFdD &proto)->void { while (!s_terminate) proto.run_tx(100); }, std::ref(proto) );

    /* Run main cycle forever */
    while (!s_terminate)
    {
        if (s_generatorEnabled)
        {
            Tiny::PacketD packet(s_packetSize);
            packet.put("Generated frame");
            if ( proto.write( packet.data(), packet.size() ) < 0 )
            {
                fprintf( stderr, "Failed to send packet\n" );
            }
        }
        else
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
    rxThread.join();
    txThread.join();
    proto.end();
    return 0;
}

int main(int argc, char *argv[])
{
    if ( parse_args(argc, argv) < 0 )
    {
        print_help();
        return 1;
    }

    SerialHandle hPort = OpenSerial(s_port, SERIAL_115200);

    if ( hPort == INVALID_SERIAL )
    {
        fprintf(stderr, "Error opening serial port\n");
        return 1;
    }

    int result = -1;
    switch (s_protocol)
    {
        case protocol_type_t::HD: result = run_hd(hPort); break;
        case protocol_type_t::FD: result = run_fd(hPort); break;
        default: fprintf(stderr, "Unknown protocol type"); break;
    }
    CloseSerial(hPort);
    return result;
}
