#include "knxnet.h"


knxnet::KNXnet::KNXnet(const char *interface_ip, address_t &physical_addr)
{
	socket_recv_fd = 0;
	socket_send_fd = 0;
	this->physical_addr = physical_addr;
	command = {};
	sin = {};

	sin.sin_family = PF_INET;
	sin.sin_addr.s_addr = INADDR_ANY;
	sin.sin_port = htons(MULTICAST_PORT);
	if ((socket_recv_fd = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
	{
		std::cerr << "Error opening receviving socket: " << std::strerror(errno) << std::endl;
		throw new std::exception();
	}
	
	int tmp = 1;
	if (setsockopt(socket_recv_fd, SOL_SOCKET, SO_REUSEADDR, &tmp, sizeof(tmp)) < 0)
	{
		std::cerr << "Error reusing socket: " << std::strerror(errno) << std::endl;
		close(socket_recv_fd);
		throw new std::exception();
	}

	if (bind(socket_recv_fd, (struct sockaddr *)&sin, sizeof(sin)) < 0)
	{
		std::cerr << "Error binding to address: " << std::strerror(errno) << std::endl;
		close(socket_recv_fd);
		throw new std::exception();
	}

	tmp = 1;
	if (setsockopt(socket_recv_fd, IPPROTO_IP, IP_MULTICAST_LOOP, &tmp, sizeof(tmp)) < 0)
	{
		std::cerr << "Error setting loopback: " << std::strerror(errno) << std::endl;
		close(socket_recv_fd);
		throw new std::exception();
	}

	command.imr_multiaddr.s_addr = inet_addr(MULTICAST_IP);
	command.imr_interface.s_addr = inet_addr(interface_ip);

	if (command.imr_multiaddr.s_addr == -1)
	{
		std::cerr << "Error, " MULTICAST_IP " is not a valid multicast address." << std::endl;
		close(socket_recv_fd);
		throw new std::exception();
	}

	if (setsockopt(socket_recv_fd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &command, sizeof(command)) < 0)
	{
		std::cerr << "Error adding membership: " << std::strerror(errno) << std::endl;
		close(socket_recv_fd);
		throw new std::exception();
	}

	if ((socket_send_fd = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
	{
		std::cerr << "Error opening sending socket: " << std::strerror(errno) << std::endl;
		close(socket_recv_fd);
		throw new std::exception();
	}

	struct in_addr send_if = {};
	inet_aton(interface_ip, &send_if);
	if (setsockopt(socket_send_fd, IPPROTO_IP, IP_MULTICAST_IF, &send_if, sizeof(send_if)) < 0)
	{
		std::cerr << "Error setting sending interface: " << std::strerror(errno) << std::endl;
		close(socket_recv_fd);
		close(socket_send_fd);
		throw new std::exception();
	}
}

void knxnet::KNXnet::receive(callback_t callback_func)
{
	if (callback_func == nullptr)
		return;

	uint8_t buf[1024];
	ssize_t rec = 0;
	unsigned int sin_len = sizeof(sin);

	rec = recvfrom(socket_recv_fd, buf, 1024, 0, (struct sockaddr *)&sin, &sin_len);
	if (rec == -1)
	{
		std::cerr << "Error receiving from socket: " << std::strerror(errno) << std::endl;
		throw new std::exception();
	}

    knx_ip_pkt_t *knx_pkt = (knx_ip_pkt_t *)buf;

    if (knx_pkt->header_len != 0x06 && knx_pkt->protocol_version != 0x10 && knx_pkt->service_type != KNX_ST_ROUTING_INDICATION)
        return;

    cemi_msg_t *cemi_msg = (cemi_msg_t *)knx_pkt->pkt_data;

    if (cemi_msg->message_code != KNX_MT_L_DATA_IND)
        return;

    cemi_service_t *cemi_data = &cemi_msg->data.service_information;

    if (cemi_msg->additional_info_len > 0)
        cemi_data = (cemi_service_t *)(((uint8_t *)cemi_data) + cemi_msg->additional_info_len);

    if (cemi_data->control_2.bits.dest_addr_type != 0x01)
        return;

    knx_command_type_t ct = (knx_command_type_t)(((cemi_data->data[0] & 0xC0) >> 6) | ((cemi_data->pci.apci & 0x03) << 2));

	uint8_t data[cemi_data->data_len];
	memcpy(data, cemi_data->data, cemi_data->data_len);
	data[0] &= 0x3F;

	message_t msg;
	msg.ct = ct;
	msg.data = data;
	msg.data_len = cemi_data->data_len;
	msg.sender = cemi_data->source;
	msg.receiver = cemi_data->destination;

	callback_func(msg);
}

void knxnet::KNXnet::send(message_t &msg)
{
    uint32_t len = 6 + 2 + 8 + msg.data_len; // knx_pkt + cemi_msg + cemi_service + data
    uint8_t buf[len];
    knx_ip_pkt_t *knx_pkt = (knx_ip_pkt_t *)buf;
    knx_pkt->header_len = 0x06;
    knx_pkt->protocol_version = 0x10;
    knx_pkt->service_type = htons(KNX_ST_ROUTING_INDICATION);
    knx_pkt->total_len.len = htons(len);
    cemi_msg_t *cemi_msg = (cemi_msg_t *)knx_pkt->pkt_data;
    cemi_msg->message_code = KNX_MT_L_DATA_IND;
    cemi_msg->additional_info_len = 0;
    cemi_service_t *cemi_data = &cemi_msg->data.service_information;
    cemi_data->control_1.bits.confirm = 0;
    cemi_data->control_1.bits.ack = 0;
    cemi_data->control_1.bits.priority = 0b11;
    cemi_data->control_1.bits.system_broadcast = 0x01;
    cemi_data->control_1.bits.repeat = 0x01;
    cemi_data->control_1.bits.reserved = 0;
    cemi_data->control_1.bits.frame_type = 0x01;
    cemi_data->control_2.bits.extended_frame_format = 0x00;
    cemi_data->control_2.bits.hop_count = 0x06;
    cemi_data->control_2.bits.dest_addr_type = 0x01;
    cemi_data->source = msg.sender;
    cemi_data->destination = msg.receiver;
    //cemi_data->destination.bytes.high = (area << 3) | line;
    //cemi_data->destination.bytes.low = member;
    cemi_data->data_len = msg.data_len;
    cemi_data->pci.apci = (msg.ct & 0x0C) >> 2;
    cemi_data->pci.tpci_seq_number = 0x00; // ???
    cemi_data->pci.tpci_comm_type = KNX_COT_UDP; // ???
    memcpy(cemi_data->data, msg.data, msg.data_len);
    cemi_data->data[0] = (cemi_data->data[0] & 0x3F) | ((msg.ct & 0x03) << 6);

    struct sockaddr_in address = {};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = inet_addr(MULTICAST_IP);
    address.sin_port = htons(MULTICAST_PORT);

    if (sendto(socket_send_fd, buf, len, 0, (struct sockaddr *)&address, sizeof(address)) < 0)
    {
		std::cerr << "Error sending: " << std::strerror(errno) << std::endl;
		throw new std::exception();
    }	
}

/**
 * Conversion functions
 */

bool knxnet::data_to_bool(uint8_t *data)
{
	return (data[0] & 0x01) == 1 ? true : false;
}

int8_t knxnet::data_to_1byte_int(uint8_t *data)
{
	return (int8_t)data[1];
}

uint8_t knxnet::data_to_1byte_uint(uint8_t *data)
{
	return data[1];
}

int16_t knxnet::data_to_2byte_int(uint8_t *data)
{
	return (int16_t)((data[1] << 8) | data[2]);
}

uint16_t knxnet::data_to_2byte_uint(uint8_t *data)
{
	return (uint16_t)((data[1] << 8) | data[2]);
}

float knxnet::data_to_2byte_float(uint8_t *data)
{
	uint8_t sign =  (data[1] & 0b10000000) >> 7;
	uint8_t expo =  (data[1] & 0b01111000) >> 3;
	union {
		uint8_t b[2];
		int16_t i;
	} mant;
	mant.b[1] = (data[1] & 0b00000111);
	mant.b[0] = data[2];
	if (sign)
	{
		// negative
		mant.i = ~mant.i;
		mant.b[1] &= 0x07;
		mant.i += 1;
	}

	return 0.01f * mant.i * pow(2, expo);
}

/*
time_of_day_t knxnet::data_to_3byte_time(uint8_t *data)
{
    time_of_day_t time;
    time.weekday = (weekday_t)((data[1] & 0b11100000) >> 5);
    time.hours = (data[1] & 0b00011111);
    time.minutes = (data[2] & 0b00111111);
    time.seconds = (data[3] & 0b00111111);
    return time;
}

date_t knxnet::data_to_3byte_data(uint8_t *data)
{
    date_t date;
    date.day = (data[1] & 0b00011111);
    date.month = (data[2] & 0b00001111);
    date.year = (data[3] & 0b01111111);
    return date;
}

color_t knxnet::data_to_3byte_color(uint8_t *data)
{
    color_t color;
    color.red = data[1];
    color.green = data[2];
    color.blue = data[3];
    return color;
}
*/

int32_t knxnet::data_to_4byte_int(uint8_t *data)
{
	return (int32_t)((data[1] << 24) | (data[2] << 16) | (data[3] << 8) | (data[4] << 0));
}

uint32_t knxnet::data_to_4byte_uint(uint8_t *data)
{
	return (uint32_t)((data[1] << 24) | (data[2] << 16) | (data[3] << 8) | (data[4] << 0));
}

float knxnet::data_to_4byte_float(uint8_t *data)
{
	union {
		float f;
		uint8_t b[4];
	} f;
	f.b[0] = data[4];
	f.b[1] = data[3];
	f.b[2] = data[2];
	f.b[3] = data[1];
	return f.f;
}

