#include <SFML/Graphics.hpp>
#include <SFML/Network.hpp>
#include <windows.h>
#include <iostream>
#include <unordered_map>
#include <chrono>
#include <string>

using namespace std;
using namespace chrono;

#include "..\..\2020-2_gameserver_termproject\2020-2_gameserver_termproject\protocol.h"

sf::TcpSocket g_socket;

constexpr auto SCREEN_WIDTH = 20;
constexpr auto SCREEN_HEIGHT = 20;

constexpr auto TILE_WIDTH = 65;
constexpr auto WINDOW_WIDTH = TILE_WIDTH * SCREEN_WIDTH / 2 + 10;   // size of window
constexpr auto WINDOW_HEIGHT = TILE_WIDTH * SCREEN_WIDTH / 2 + 10;
constexpr auto BUF_SIZE = 200;

// 추후 확장용.
int NPC_ID_START = 10000;

int g_left_x;
int g_top_y;
int g_myid;
short g_level;
short g_hp;
int g_exp;

sf::RenderWindow* g_window;
sf::Font g_font;

class OBJECT {
private:
	bool m_showing;
	sf::Sprite m_sprite;

	char m_mess[MAX_STR_LEN];
	high_resolution_clock::time_point m_time_out;
	sf::Text m_text;
	sf::Text m_name;

	sf::Text m_level;
	sf::Text m_exp;
	sf::Text m_hp;

public:
	int m_x, m_y;
	char name[MAX_ID_LEN];
	OBJECT(sf::Texture& t, int x, int y, int x2, int y2) {
		m_showing = false;
		m_sprite.setTexture(t);
		m_sprite.setTextureRect(sf::IntRect(x, y, x2, y2));
		m_time_out = high_resolution_clock::now();
	}
	OBJECT() {
		m_showing = false;
		m_time_out = high_resolution_clock::now();
	}
	void show()
	{
		m_showing = true;
	}
	void hide()
	{
		m_showing = false;
	}

	void a_move(int x, int y) {
		m_sprite.setPosition((float)x, (float)y);
	}

	void a_draw() {
		g_window->draw(m_sprite);
	}

	void move(int x, int y) {
		m_x = x;
		m_y = y;
	}
	void draw() {
		if (false == m_showing) return;
		float rx = (m_x - g_left_x) * 65.0f + 8;
		float ry = (m_y - g_top_y) * 65.0f + 8;
		m_sprite.setPosition(rx, ry);
		g_window->draw(m_sprite);
		m_name.setPosition(rx - 10, ry - 10);
		m_hp.setPosition(rx - 500, ry - 600);
		m_level.setPosition(rx -150 , ry - 600);
		m_exp.setPosition(rx + 250, ry - 600);
		g_window->draw(m_name);
		g_window->draw(m_hp);
		g_window->draw(m_level);
		g_window->draw(m_exp);
		if (high_resolution_clock::now() < m_time_out) {
			m_text.setPosition(rx - 10, ry + 40);
			g_window->draw(m_text);
		}
	}
	void set_name(char str[]) {
		m_name.setFont(g_font);
		m_name.setString(str);
		m_name.setFillColor(sf::Color(255, 255, 0));
		m_name.setStyle(sf::Text::Bold);
	}
	void add_chat(char chat[]) {
		m_text.setFont(g_font);
		m_text.setString(chat);
		m_time_out = high_resolution_clock::now() + 1s;
	}

	void set_exp(int exp)
	{
		char exp_string[100];
		m_exp.setFont(g_font);
		sprintf_s(exp_string, "EXP : %d", exp);
		m_exp.setString(exp_string);
		m_exp.setCharacterSize(60);
		m_exp.setFillColor(sf::Color(255, 0, 255));
		m_exp.setStyle(sf::Text::Bold);
	}

	void set_level(short level)
	{
		char level_string[100];
		m_level.setFont(g_font);
		sprintf_s(level_string, "LEVEL : %d", level);
		m_level.setString(level_string);
		m_level.setCharacterSize(60);
		m_level.setFillColor(sf::Color(255, 0, 255));
		m_level.setStyle(sf::Text::Bold);
	}

	void set_hp(short hp)
	{
		char hp_string[100];
		m_hp.setFont(g_font);
		sprintf_s(hp_string, "HP : %d", hp);
		m_hp.setString(hp_string);
		m_hp.setCharacterSize(60);
		m_hp.setFillColor(sf::Color(255, 0, 255));
		m_hp.setStyle(sf::Text::Bold);
	}

};

OBJECT avatar;
unordered_map <int, OBJECT> npcs;

OBJECT white_tile;
OBJECT black_tile;
OBJECT fire_effect;

sf::Texture* board;
sf::Texture* pieces;
sf::Texture* fire;

void Play_Attack_effect(int id)
{
	int a_x, a_y;

	if (id != g_myid)
	{
		a_x = npcs[id].m_x;
		a_y = npcs[id].m_y;
	}
	else
	{
		a_x = avatar.m_x;
		a_y = avatar.m_y;
	}

	OBJECT range[4];
	range[0].m_x = (a_x - 1 - g_left_x) * 65.0f + 8;;
	range[0].m_y = (a_y - g_top_y) * 65.0f + 8;

	range[1].m_x = (a_x + 1 - g_left_x) * 65.0f + 8;
	range[1].m_y = (a_y - g_top_y) * 65.0f + 8;

	range[2].m_x = (a_x - g_left_x) * 65.0f + 8;
	range[2].m_y = (a_y - 1 - g_top_y) * 65.0f + 8;

	range[3].m_x = (a_x - g_left_x) * 65.0f + 8;
	range[3].m_y = (a_y + 1 - g_top_y) * 65.0f + 8;

	for (int i = 0; i < 4; ++i)
	{
		fire_effect.a_move(range[i].m_x, range[i].m_y);
		fire_effect.a_draw();
	}
}

void client_initialize()
{
	board = new sf::Texture;
	pieces = new sf::Texture;
	fire = new sf::Texture;

	if (false == g_font.loadFromFile("cour.ttf")) {
		cout << "Font Loading Error!\n";
		while (true);
	}
	board->loadFromFile("chessmap.bmp");
	pieces->loadFromFile("chess2.png");
	fire->loadFromFile("Fire.png");
	white_tile = OBJECT{ *board, 5, 5, TILE_WIDTH, TILE_WIDTH };
	black_tile = OBJECT{ *board, 69, 5, TILE_WIDTH, TILE_WIDTH };
	avatar = OBJECT{ *pieces, 128, 0, 64, 64 };
	avatar.move(4, 4);

	fire_effect = OBJECT{ *fire, 0, 0, 200, 200 };
}

void client_finish()
{
	delete board;
	delete pieces;
}

void ProcessPacket(char* ptr)
{
	static bool first_time = true;
	switch (ptr[1])
	{
	case SC_PACKET_LOGIN_OK:
	{
		sc_packet_login_ok* my_packet = reinterpret_cast<sc_packet_login_ok*>(ptr);
		g_myid = my_packet->id;
		avatar.move(my_packet->x, my_packet->y);
		g_left_x = my_packet->x - (SCREEN_WIDTH / 2);
		g_top_y = my_packet->y - (SCREEN_HEIGHT / 2);
		g_hp = my_packet->hp;
		avatar.set_hp(g_hp);
		g_level = my_packet->level;
		avatar.set_level(g_level);
		g_exp = my_packet->exp;
		avatar.set_exp(g_exp);
		avatar.show();
	}
	break;

	case SC_PACKET_ENTER:
	{
		sc_packet_enter* my_packet = reinterpret_cast<sc_packet_enter*>(ptr);
		int id = my_packet->id;

		if (id == g_myid) {
			avatar.move(my_packet->x, my_packet->y);
			g_left_x = my_packet->x - (SCREEN_WIDTH / 2);
			g_top_y = my_packet->y - (SCREEN_HEIGHT / 2);
			avatar.show();
		}
		else {
			if (id < NPC_ID_START)
				npcs[id] = OBJECT{ *pieces, 64, 0, 64, 64 };
			else
				npcs[id] = OBJECT{ *pieces, 0, 0, 64, 64 };
			strcpy_s(npcs[id].name, my_packet->name);
			npcs[id].set_name(my_packet->name);
			npcs[id].move(my_packet->x, my_packet->y);
			npcs[id].show();
		}
	}
	break;
	case SC_PACKET_MOVE:
	{
		sc_packet_move* my_packet = reinterpret_cast<sc_packet_move*>(ptr);
		int other_id = my_packet->id;
		if (other_id == g_myid) {
			avatar.move(my_packet->x, my_packet->y);
			g_left_x = my_packet->x - (SCREEN_WIDTH / 2);
			g_top_y = my_packet->y - (SCREEN_HEIGHT / 2);
		}
		else {
			if (0 != npcs.count(other_id))
				npcs[other_id].move(my_packet->x, my_packet->y);
		}
	}
	break;

	case SC_PACKET_LEAVE:
	{
		sc_packet_leave* my_packet = reinterpret_cast<sc_packet_leave*>(ptr);
		int other_id = my_packet->id;
		if (other_id == g_myid) {
			avatar.hide();
		}
		else {
			if (0 != npcs.count(other_id))
				npcs[other_id].hide();
		}
	}
	break;

	case SC_PACKET_CHAT:
	{
		sc_packet_chat* my_packet = reinterpret_cast<sc_packet_chat*>(ptr);

		npcs[my_packet->id].add_chat(my_packet->message);
	}
	break;

	case SC_PACKET_STAT_CHANGE:
	{
		sc_packet_stat_change* my_packet = reinterpret_cast<sc_packet_stat_change*>(ptr);
		int id = my_packet->id;
		if (id == g_myid)
		{
			g_hp = my_packet->hp;
			avatar.set_hp(g_hp);
			g_exp = my_packet->exp;
			avatar.set_exp(g_exp);
			g_level = my_packet->level;
			avatar.set_level(g_level);
		}
	}
	break;
	case SC_PACKET_ATTACK_EFFECT:
	{
		sc_packet_attack_effect* my_packet = reinterpret_cast<sc_packet_attack_effect*>(ptr);
		int id = my_packet->id;
		Play_Attack_effect(id);
	}
	break;
	default:
		printf("Unknown PACKET type [%d]\n", ptr[1]);

	}
}

void process_data(char* net_buf, size_t io_byte)
{
	char* ptr = net_buf;
	static size_t in_packet_size = 0;
	static size_t saved_packet_size = 0;
	static char packet_buffer[BUF_SIZE];

	while (0 != io_byte) {
		if (0 == in_packet_size) in_packet_size = ptr[0];
		if (io_byte + saved_packet_size >= in_packet_size) {
			memcpy(packet_buffer + saved_packet_size, ptr, in_packet_size - saved_packet_size);
			ProcessPacket(packet_buffer);
			ptr += in_packet_size - saved_packet_size;
			io_byte -= in_packet_size - saved_packet_size;
			in_packet_size = 0;
			saved_packet_size = 0;
		}
		else {
			memcpy(packet_buffer + saved_packet_size, ptr, io_byte);
			saved_packet_size += io_byte;
			io_byte = 0;
		}
	}
}

void client_main()
{
	char net_buf[BUF_SIZE];
	size_t	received;

	auto recv_result = g_socket.receive(net_buf, BUF_SIZE, received);
	if (recv_result == sf::Socket::Error)
	{
		wcout << L"Recv 에러!";
		while (true);
	}

	if (recv_result == sf::Socket::Disconnected)
	{
		wcout << L"서버 접속 종료.";
		g_window->close();
	}

	if (recv_result != sf::Socket::NotReady)
		if (received > 0) process_data(net_buf, received);

	for (int i = 0; i < SCREEN_WIDTH; ++i) {
		int tile_x = i + g_left_x;
		if (tile_x >= WORLD_WIDTH) break;
		if (tile_x < 0) continue;
		for (int j = 0; j < SCREEN_HEIGHT; ++j)
		{
			int tile_y = j + g_top_y;
			if (tile_y >= WORLD_HEIGHT) break;
			if (tile_y < 0) continue;
			if (((tile_x / 3 + tile_y / 3) % 2) == 0) {
				white_tile.a_move(TILE_WIDTH * i + 7, TILE_WIDTH * j + 7);
				white_tile.a_draw();
			}
			else
			{
				black_tile.a_move(TILE_WIDTH * i + 7, TILE_WIDTH * j + 7);
				black_tile.a_draw();
			}
		}
	}
	
	avatar.draw();
	//	for (auto &pl : players) pl.draw();
	for (auto& npc : npcs) npc.second.draw();
	sf::Text text;
	text.setFont(g_font);
	char buf[100];
	sprintf_s(buf, "(%d, %d)", avatar.m_x, avatar.m_y);
	text.setString(buf);
	g_window->draw(text);

}

void send_packet(void* packet)
{
	char* p = reinterpret_cast<char*>(packet);
	size_t sent;
	sf::Socket::Status st = g_socket.send(p, p[0], sent);
	int a = 3;
}

void send_move_packet(unsigned char dir)
{
	cs_packet_move m_packet;
	m_packet.type = CS_MOVE;
	m_packet.size = sizeof(m_packet);
	m_packet.direction = dir;
	send_packet(&m_packet);
}

void send_attack_packet()
{
	cs_packet_attack a_packet;
	a_packet.size = sizeof(a_packet);
	a_packet.type = CS_ATTACK;
	send_packet(&a_packet);
}

int main()
{
	wcout.imbue(locale("korean"));
	sf::Socket::Status status = g_socket.connect("127.0.0.1", SERVER_PORT);
	g_socket.setBlocking(false);

	if (status != sf::Socket::Done) {
		wcout << L"서버와 연결할 수 없습니다.\n";
		while (true);
	}

	char ID[MAX_ID_LEN];
	cout << "ID를 입력하세요 : ";
	cin >> ID;

	client_initialize();

	cs_packet_login l_packet;
	l_packet.size = sizeof(l_packet);
	l_packet.type = CS_LOGIN;
	//int t_id = GetCurrentProcessId();
	//sprintf_s(l_packet.name, "P%03d", t_id % 1000);
	strcpy_s(l_packet.name, MAX_ID_LEN, ID);
	strcpy_s(avatar.name, l_packet.name);
	avatar.set_name(l_packet.name);
	send_packet(&l_packet);

	sf::RenderWindow window(sf::VideoMode(WINDOW_WIDTH, WINDOW_HEIGHT), "2D CLIENT");
	g_window = &window;

	sf::View view = g_window->getView();
	view.zoom(2.0f);
	view.move(SCREEN_WIDTH * TILE_WIDTH / 4, SCREEN_HEIGHT * TILE_WIDTH / 4);
	g_window->setView(view);

	while (window.isOpen())
	{
		sf::Event event;
		while (window.pollEvent(event))
		{
			if (event.type == sf::Event::Closed)
				window.close();
			if (event.type == sf::Event::KeyPressed) {
				int p_type = -1;
				switch (event.key.code) {
				case sf::Keyboard::Left:
					send_move_packet(MV_LEFT);
					break;
				case sf::Keyboard::Right:
					send_move_packet(MV_RIGHT);
					break;
				case sf::Keyboard::Up:
					send_move_packet(MV_UP);
					break;
				case sf::Keyboard::Down:
					send_move_packet(MV_DOWN);
					break;
				case sf::Keyboard::A:
					send_attack_packet();
					break;
				case sf::Keyboard::Escape:
					window.close();
					break;
				}
			}
		}

		window.clear();
		client_main();
		window.display();
	}
	client_finish();

	return 0;
}