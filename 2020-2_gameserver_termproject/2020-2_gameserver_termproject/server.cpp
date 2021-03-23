#include <iostream>
#include <WS2tcpip.h>
#include <MSWSock.h>
#include <thread>
#include <vector>
#include <mutex>
#include <unordered_set>
#include <chrono>
#include <queue>

#include <windows.h>  
#include <sqlext.h>  

extern "C" {
#include "include/lua.h"
#include "include/lauxlib.h"
#include "include/lualib.h"
}

#include "protocol.h"
using namespace std;
using namespace chrono;
#pragma comment(lib, "Ws2_32.lib")
#pragma comment(lib, "MSWSock.lib")
#pragma comment(lib, "lua54.lib")

#pragma comment(lib, "odbc32.lib")

constexpr int MAX_BUFFER = 4096;

constexpr char OP_MODE_RECV = 0;
constexpr char OP_MODE_SEND = 1;
constexpr char OP_MODE_ACCEPT = 2;
constexpr char OP_RANDOM_MOVE = 3;
constexpr char OP_PLAYER_MOVE_NOTIFY = 4;
constexpr char OP_MOVE_COUNT = 5;
constexpr char OP_ATTACK = 6;
constexpr char OP_RESURRECTION = 7;
constexpr char OP_HP_HEALING = 8;

constexpr char OP_ODBC_SEARCH = 9;
constexpr char OP_ODBC_INSERT = 10;
constexpr char OP_ODBC_UPDATE_LEVEL = 11;
constexpr char OP_ODBC_UPDATE_HP = 12;
constexpr char OP_ODBC_UPDATE_EXP = 13;

constexpr int  KEY_SERVER = 1000000;

constexpr int SECTOR_WIDTH = 16;
constexpr int SECTOR_HEIGHT = 16;

constexpr int ATTACK_DAMAGE = 10;
constexpr int MULTIPLE_EXP = 2;

void show_error(SQLHANDLE hHandle, SQLSMALLINT hType, RETCODE RetCode) {
	SQLSMALLINT iRec = 0;
	SQLINTEGER iError;
	WCHAR wszMessage[1000];
	WCHAR wszState[SQL_SQLSTATE_SIZE + 1];

	if (RetCode == SQL_INVALID_HANDLE) {
		wcout << L"Invalid handle!\n";
		return;
	}
	while (SQLGetDiagRec(hType, hHandle, ++iRec, wszState, &iError, wszMessage,
		(SQLSMALLINT)(sizeof(wszMessage) / sizeof(WCHAR)), (SQLSMALLINT*)NULL) == SQL_SUCCESS) {
		// Hide data truncated..
		if (wcsncmp(wszState, L"01004", 5)) {
			wcout << L"[" << wszState << "]" << wszMessage << "(" << iError << ")\n";
		}
	}
}

int odbc_work() {

	SQLHENV henv;
	SQLHDBC hdbc;
	SQLHSTMT hstmt = 0;
	SQLRETURN retcode;
	SQLWCHAR szName[MAX_ID_LEN];
	SQLINTEGER ID = 0, LEVEL = 0, HP = 0, EXP = 0;
	SQLLEN cbName = 0, cbID = 0, cbLEVEL = 0, cbHP = 0, cbEXP = 0;

	// Allocate environment handle  
	retcode = SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &henv);
	// Set the ODBC version environment attribute  
	if (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO) {
		retcode = SQLSetEnvAttr(henv, SQL_ATTR_ODBC_VERSION, (SQLPOINTER*)SQL_OV_ODBC3, 0);

		// Allocate connection handle  
		if (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO) {
			retcode = SQLAllocHandle(SQL_HANDLE_DBC, henv, &hdbc);

			// Set login timeout to 5 seconds  
			if (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO) {
				SQLSetConnectAttr(hdbc, SQL_LOGIN_TIMEOUT, (SQLPOINTER)5, 0);

				// Connect to data source  
				retcode = SQLConnect(hdbc, (SQLWCHAR*)L"2020_2_gameserver", SQL_NTS, (SQLWCHAR*)NULL, 0, NULL, 0);

				// Allocate statement handle  
				if (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO) {
					retcode = SQLAllocHandle(SQL_HANDLE_STMT, hdbc, &hstmt);

					retcode = SQLExecDirect(hstmt, (SQLWCHAR*)L"EXEC SearchName TEST ", SQL_NTS);
					if (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO) {

						// Bind columns 1, 2, and 3  
						retcode = SQLBindCol(hstmt, 1, SQL_INTEGER, &ID, 100, &cbID);
						retcode = SQLBindCol(hstmt, 2, SQL_C_WCHAR, szName, MAX_ID_LEN, &cbName);
						retcode = SQLBindCol(hstmt, 3, SQL_INTEGER, &LEVEL, 100, &cbLEVEL);
						retcode = SQLBindCol(hstmt, 4, SQL_INTEGER, &HP, 100, &cbHP);
						retcode = SQLBindCol(hstmt, 5, SQL_INTEGER, &EXP, 100, &cbEXP);

						// Fetch and print each row of data. On an error, display a message and exit.  
						for (int i = 0; ; i++) {
							retcode = SQLFetch(hstmt);
							if (retcode == SQL_ERROR || retcode == SQL_SUCCESS_WITH_INFO)
								show_error(hstmt, SQL_HANDLE_STMT, retcode );
							if (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO)
							{
								wcout << "ID : " << ID << " NAME : " << szName << " LEVEL : " << LEVEL << " HP : " << HP << " EXP : " << EXP << endl;
							}
							else
								break;
						}
					}
					else
					{
						show_error(hstmt, SQL_HANDLE_STMT, retcode);
						cout << "SQL Query Error!\n";
					}
					// Process data  
					if (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO) {
						SQLCancel(hstmt);
						SQLFreeHandle(SQL_HANDLE_STMT, hstmt);
					}

					SQLDisconnect(hdbc);
				}

				SQLFreeHandle(SQL_HANDLE_DBC, hdbc);
			}
		}
		SQLFreeHandle(SQL_HANDLE_ENV, henv);
	}

	return 0;
}

struct OVER_EX {
	WSAOVERLAPPED wsa_over;
	char	op_mode;
	WSABUF	wsa_buf;
	unsigned char iocp_buf[MAX_BUFFER];
	int		object_id;
};

struct client_info {
	mutex	c_lock;
	char	name[MAX_ID_LEN];
	short	x, y;

	short	level;
	short	hp;
	int		exp;

	bool can_attack;

	lua_State* L;

	bool in_use;
	bool is_active;
	SOCKET	m_sock;
	OVER_EX	m_recv_over;
	unsigned char* m_packet_start;
	unsigned char* m_recv_start;

	mutex vl;
	unordered_set <int> view_list;

	int move_time;

	mutex lua_l;
	bool overlapped;
	int move_count;

	int sec_x;
	int sec_y;

};

mutex id_lock;
client_info g_clients[MAX_USER + NUM_NPC];
HANDLE		h_iocp;

SOCKET g_lSocket;
OVER_EX g_accept_over;

mutex sec_lock;
unordered_set<int> sector[WORLD_WIDTH / SECTOR_WIDTH][WORLD_HEIGHT / SECTOR_HEIGHT];

struct event_type {
	int obj_id;
	system_clock::time_point wakeup_time;
	int event_id;
	int target_id;

	constexpr bool operator < (const event_type& _Left) const
	{
		return (wakeup_time > _Left.wakeup_time);
	}
};

priority_queue<event_type> timer_queue;
mutex timer_l;

bool CAS(volatile bool* addr, bool expected, bool new_val)
{
	return atomic_compare_exchange_strong(reinterpret_cast<volatile atomic_bool*>(addr), 
		&expected, new_val);
}

int get_sector_x_index(int pos_x)
{
	for (int i = 0; i < WORLD_WIDTH / SECTOR_WIDTH; ++i)
	{
		if (i * SECTOR_WIDTH <= pos_x && pos_x < (i + 1) * SECTOR_WIDTH)
		{
			return i;
		}
	}
}

int get_sector_y_index(int pos_y)
{
	for (int i = 0; i < WORLD_WIDTH / SECTOR_HEIGHT; ++i)
	{
		if (i * SECTOR_HEIGHT <= pos_y && pos_y < (i + 1) * SECTOR_HEIGHT)
		{
			return i;
		}
	}
}

void insert_obj_in_sector(int id, int idx_x, int idx_y)
{
	sec_lock.lock();
	sector[idx_x][idx_y].insert(id);
	sec_lock.unlock();
}

void erase_obj_in_sector(int id, int idx_x, int idx_y)
{
	sec_lock.lock();
	sector[idx_x][idx_y].erase(id);
	sec_lock.unlock();
}

void add_timer(int obj_id, int ev_type, system_clock::time_point t)
{
	event_type ev{ obj_id, t, ev_type };
	timer_l.lock();
	timer_queue.push(ev);
	timer_l.unlock();
}

void wake_up_npc(int id)
{
	if (false == g_clients[id].is_active)
	{
		//g_clients[id].is_active = true;      // CAS로 구현해서 이중 활성화를 막아야 한다.
		if (true == CAS(&g_clients[id].is_active, false, true))
		{
			add_timer(id, OP_RANDOM_MOVE, system_clock::now() + 1s);
		}
	}
}

bool is_near(int p1, int p2)
{
	int dist = (g_clients[p1].x - g_clients[p2].x) * (g_clients[p1].x - g_clients[p2].x);
	dist += (g_clients[p1].y - g_clients[p2].y) * (g_clients[p1].y - g_clients[p2].y);

	return dist <= VIEW_LIMIT * VIEW_LIMIT;
}

bool is_npc(int p1)
{
	return p1 >= MAX_USER;
}

void get_obj_list_in_near_sector(unordered_set<int>* obj_list, int idx_x, int idx_y)
{
	for (int i = idx_x - 1; i < idx_x + 2; ++i)
	{
		if (i < 0 || i >= WORLD_WIDTH / SECTOR_WIDTH) continue;
		for (int j = idx_y - 1; j < idx_y + 2; ++j)
		{
			if (j < 0 || j >= WORLD_HEIGHT / SECTOR_HEIGHT) continue;
			sec_lock.lock();
			for (const auto& p : sector[i][j])
			{
				obj_list->emplace(p);
			}
			sec_lock.unlock();
		}
	}
}

void random_move_npc(int id);

void time_worker()
{
	while (true) {
		while (true) {
			if (false == timer_queue.empty()) {
				event_type ev = timer_queue.top();
				if (ev.wakeup_time > system_clock::now()) break;
				timer_queue.pop();

				switch (ev.event_id)
				{
					case OP_RANDOM_MOVE:
					{
						OVER_EX* ex_over = new OVER_EX;
						ex_over->op_mode = OP_RANDOM_MOVE;
						PostQueuedCompletionStatus(h_iocp, 1, ev.obj_id, &ex_over->wsa_over);
					}
					break;
					case OP_ATTACK:
					{
						OVER_EX* ex_over = new OVER_EX;
						ex_over->op_mode = OP_ATTACK;
						PostQueuedCompletionStatus(h_iocp, 1, ev.obj_id, &ex_over->wsa_over);
					}
					break;
					case OP_RESURRECTION:
					{
						OVER_EX* ex_over = new OVER_EX;
						ex_over->op_mode = OP_RESURRECTION;
						PostQueuedCompletionStatus(h_iocp, 1, ev.obj_id, &ex_over->wsa_over);
					}
					break;
					case OP_HP_HEALING:
					{
						OVER_EX* ex_over = new OVER_EX;
						ex_over->op_mode = OP_HP_HEALING;
						PostQueuedCompletionStatus(h_iocp, 1, ev.obj_id, &ex_over->wsa_over);
					}
					break;
				}
			}
			else break;
		}
		this_thread::sleep_for(1ms);
	}
}

void error_display(const char* msg, int err_no)
{
	WCHAR* lpMsgBuf;
	FormatMessage(
		FORMAT_MESSAGE_ALLOCATE_BUFFER |
		FORMAT_MESSAGE_FROM_SYSTEM,
		NULL, err_no,
		MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
		(LPTSTR)&lpMsgBuf, 0, NULL);
	std::cout << msg;
	std::wcout << L"에러 " << lpMsgBuf << std::endl;
	while (true);
	LocalFree(lpMsgBuf);
}

void send_packet(int id, void* p)
{
	unsigned char* packet = reinterpret_cast<unsigned char*>(p);
	OVER_EX* send_over = new OVER_EX;
	memcpy(send_over->iocp_buf, packet, packet[0]);
	send_over->op_mode = OP_MODE_SEND;
	send_over->wsa_buf.buf = reinterpret_cast<CHAR*>(send_over->iocp_buf);
	send_over->wsa_buf.len = packet[0];
	ZeroMemory(&send_over->wsa_over, sizeof(send_over->wsa_over));
	g_clients[id].c_lock.lock();
	if (true == g_clients[id].in_use)
		WSASend(g_clients[id].m_sock, &send_over->wsa_buf, 1,
			NULL, 0, &send_over->wsa_over, NULL);
	g_clients[id].c_lock.unlock();
}

void send_chat_packet(int to_client, int id, char* mess)
{
	sc_packet_chat p;
	p.id = id;
	p.size = sizeof(p);
	p.type = SC_PACKET_CHAT;
	strcpy_s(p.message, mess);
	send_packet(to_client, &p);
}

void send_login_ok(int id)
{
	sc_packet_login_ok p;
	p.exp = g_clients[id].exp;
	p.hp = g_clients[id].hp;
	p.id = id;
	p.level = g_clients[id].level;
	p.size = sizeof(p);
	p.type = SC_PACKET_LOGIN_OK;
	p.x = g_clients[id].x;
	p.y = g_clients[id].y;
	send_packet(id, &p);
}

void send_move_packet(int to_client, int id)
{
	sc_packet_move p;
	p.id = id;
	p.size = sizeof(p);
	p.type = SC_PACKET_MOVE;
	p.x = g_clients[id].x;
	p.y = g_clients[id].y;
	p.move_time = g_clients[id].move_time;
	send_packet(to_client, &p);
}

void send_enter_packet(int to_client, int new_id)
{
	sc_packet_enter p;
	p.id = new_id;
	p.size = sizeof(p);
	p.type = SC_PACKET_ENTER;
	p.x = g_clients[new_id].x;
	p.y = g_clients[new_id].y;
	g_clients[new_id].c_lock.lock();
	strcpy_s(p.name, g_clients[new_id].name);
	g_clients[new_id].c_lock.unlock();
	p.o_type = 0;
	send_packet(to_client, &p);
}

void send_leave_packet(int to_client, int new_id)
{
	sc_packet_leave p;
	p.id = new_id;
	p.size = sizeof(p);
	p.type = SC_PACKET_LEAVE;
	send_packet(to_client, &p);
}

void send_stat_change_packet(int to_client, int id)
{
	sc_packet_stat_change p;
	p.size = sizeof(p);
	p.type = SC_PACKET_STAT_CHANGE;
	p.id = id;
	p.hp = g_clients[id].hp;
	p.level = g_clients[id].level;
	p.exp = g_clients[id].exp;
	send_packet(to_client, &p);
}

void send_attack_effect(int to_client, int id)
{
	sc_packet_attack_effect p;
	p.size = sizeof(p);
	p.type = SC_PACKET_ATTACK_EFFECT;
	p.id = id;
	send_packet(to_client, &p);
}

void process_move(int id, char dir)
{
	short y = g_clients[id].y;
	short x = g_clients[id].x;
	switch (dir) {
	case MV_UP: if (y > 0) y--; break;
	case MV_DOWN: if (y < (WORLD_HEIGHT - 1)) y++; break;
	case MV_LEFT: if (x > 0) x--; break;
	case MV_RIGHT: if (x < (WORLD_WIDTH - 1)) x++; break;
	default: cout << "Unknown Direction in CS_MOVE packet.\n";
		while (true);
	}

	g_clients[id].vl.lock();
	unordered_set <int> old_viewlist = g_clients[id].view_list;
	g_clients[id].vl.unlock();

	g_clients[id].x = x;
	g_clients[id].y = y;

	send_move_packet(id, id);

	int sec_x = get_sector_x_index(g_clients[id].x);
	int sec_y = get_sector_y_index(g_clients[id].y);

	if (sec_x != g_clients[id].sec_x || sec_y != g_clients[id].sec_y)
	{
		g_clients[id].sec_x = sec_x;
		g_clients[id].sec_y = sec_y;
		erase_obj_in_sector(id, g_clients[id].sec_x, g_clients[id].sec_y);
		insert_obj_in_sector(id, g_clients[id].sec_x, g_clients[id].sec_y);
	}

	unordered_set <int> new_viewlist;
	unordered_set <int>* obj_list = new unordered_set<int>;
	get_obj_list_in_near_sector(obj_list, g_clients[id].sec_x, g_clients[id].sec_y);

	for (const auto& p : *obj_list)
	{
		if (id == p) continue;
		if (false == g_clients[p].in_use) continue;
		if (true == is_near(id, p)) new_viewlist.insert(p);
	}

	for (auto p : *obj_list)
	{
		if (true == is_npc(p))
		{
			if(g_clients[p].in_use == true)
				if (true == is_near(id, p)) new_viewlist.insert(p);
					wake_up_npc(p);
		}
	}

	// 시야에 들어온 객체 처리
	for (int ob : new_viewlist) {
		if (0 == old_viewlist.count(ob)) {
			g_clients[id].vl.lock();
			g_clients[id].view_list.insert(ob);
			g_clients[id].vl.unlock();
			send_enter_packet(id, ob);

			if (false == is_npc(ob)) {
				g_clients[ob].vl.lock();
				if (0 == g_clients[ob].view_list.count(id)) {
					g_clients[ob].view_list.insert(id);
					g_clients[ob].vl.unlock();
					send_enter_packet(ob, id);
				}
				else {
					g_clients[ob].vl.unlock();
					send_move_packet(ob, id);
				}
			}
		}
		else {  // 이전에도 시야에 있었고, 이동후에도 시야에 있는 객체
			if (false == is_npc(ob)) {
				g_clients[ob].vl.lock();
				if (0 != g_clients[ob].view_list.count(id)) {
					send_move_packet(ob, id);
				}
				else
				{
					g_clients[ob].view_list.insert(id);
					send_enter_packet(ob, id);
				}
				g_clients[ob].vl.unlock();
			}
		}
	}
	
	for (int ob : old_viewlist) { // 시야에서 벗어난 객체 처리
		if (0 == new_viewlist.count(ob)) {
			g_clients[id].vl.lock();
			g_clients[id].view_list.erase(ob);
			g_clients[id].vl.unlock();
			send_leave_packet(id, ob);
			if (false == is_npc(ob)) {
				g_clients[ob].vl.lock();
				if (0 != g_clients[ob].view_list.count(id)) {
					g_clients[ob].view_list.erase(id);
					send_leave_packet(ob, id);
				}
				g_clients[ob].vl.unlock();
			}
		}
	}

	if (false == is_npc(id)) {
		for (auto& npc : new_viewlist) {
			if (false == is_npc(npc)) continue;
			OVER_EX* ex_over = new OVER_EX;
			ex_over->object_id = id;
			ex_over->op_mode = OP_PLAYER_MOVE_NOTIFY;
			PostQueuedCompletionStatus(h_iocp, 1, npc, &ex_over->wsa_over);
		}
	}

	delete obj_list;
}

bool is_in_attack_range(int id, int target)
{
	if (g_clients[id].x == g_clients[target].x)
	{
		if (abs(g_clients[id].y - g_clients[target].y) <= 1)
			return true;
	}
	else if (g_clients[id].y == g_clients[target].y)
	{
		if (abs(g_clients[id].x - g_clients[target].x) <= 1)
			return true;
	}

	return false;
}

void add_hp(int id, short value)
{
	g_clients[id].hp += value;
}

void add_exp(int id, short value)
{
	g_clients[id].exp += value;
}

void add_level(int id, short value)
{
	g_clients[id].level += value;
}

int get_need_exp(int id)
{
	return pow(2, g_clients[id].level - 1) * 100;
}

void process_attack(int id)
{
	if (false == g_clients[id].can_attack) return;

	unordered_set<int> targetlist = g_clients[id].view_list;
	unordered_set<int> eraselist;

	for (const auto& other : targetlist)
	{
		if (is_npc(other))
		{
			if (true == is_in_attack_range(id, other))
			{
				g_clients[other].c_lock.lock();
				add_hp(other, g_clients[id].level * ATTACK_DAMAGE * -1);
				if (g_clients[other].hp <= 0)
				{
					g_clients[other].in_use = false;
					
					CAS(&g_clients[id].is_active, true, false);

					send_leave_packet(id, other);

					add_exp(id, g_clients[other].level * g_clients[other].level * 25);
					if (g_clients[id].exp >= get_need_exp(id))
					{
						g_clients[id].level += 1;
					}
					send_stat_change_packet(id, id);
					eraselist.insert(other);
				}
				g_clients[other].c_lock.unlock();
			}
		}
	}

	for (const auto& el : eraselist)
	{
		erase_obj_in_sector(el, g_clients[el].sec_x, g_clients[el].sec_y);

		g_clients[id].vl.lock();
		g_clients[id].view_list.erase(el);
		g_clients[id].vl.unlock();

		g_clients[el].vl.lock();
		g_clients[el].view_list.clear();
		g_clients[el].vl.unlock();		

		add_timer(el, OP_RESURRECTION, system_clock::now() + 5s);
	}

	g_clients[id].c_lock.lock();
	g_clients[id].can_attack = false;
	g_clients[id].c_lock.unlock();
	add_timer(id, OP_ATTACK, system_clock::now() + 1s);
}

void process_Healing(int id)
{
	if (g_clients[id].hp < 100)
	{
		g_clients[id].c_lock.lock();
		add_hp(id, 10);
		if (g_clients[id].hp > 100)
			g_clients[id].hp = 100;
		g_clients[id].c_lock.unlock();

		send_stat_change_packet(id, id);
	}

	add_timer(id, OP_HP_HEALING, system_clock::now() + 5s);
}

void process_packet(int id)
{
	char p_type = g_clients[id].m_packet_start[1];
	switch (p_type) {
	case CS_LOGIN: {
		cs_packet_login* p = reinterpret_cast<cs_packet_login*>(g_clients[id].m_packet_start);
		g_clients[id].c_lock.lock();
		strcpy_s(g_clients[id].name, p->name);
		g_clients[id].c_lock.unlock();
		send_login_ok(id);
		process_Healing(id);

		unordered_set<int>* obj_list = new unordered_set<int>;
		get_obj_list_in_near_sector(obj_list, g_clients[id].sec_x, g_clients[id].sec_y);

		for (const auto& p : *obj_list)
		{
			if (true == g_clients[p].in_use)
				if (id != p) {
					if (false == is_near(p, id)) continue;
					if (0 == g_clients[p].view_list.count(id)) {
						g_clients[p].vl.lock();
						g_clients[p].view_list.insert(id);
						g_clients[p].vl.unlock();
						send_enter_packet(p, id);
					}
					if (0 == g_clients[id].view_list.count(p)) {
						g_clients[p].vl.lock();
						g_clients[id].view_list.insert(p);
						g_clients[p].vl.unlock();
						send_enter_packet(id, p);
					}
				}
		}

		for (const auto& p : *obj_list)
		{
			if (true == is_npc(p))
			{
				if (false == is_near(id, p)) continue;
				g_clients[id].view_list.insert(p);
				send_enter_packet(id, p);
				wake_up_npc(p);
			}
		}
		delete obj_list;
		break;
	}
	case CS_MOVE: {
		cs_packet_move* p = reinterpret_cast<cs_packet_move*>(g_clients[id].m_packet_start);
		g_clients[id].move_time = p->move_time;
		process_move(id, p->direction);
		break;
	}
	case CS_ATTACK:
	{
		cs_packet_attack* p = reinterpret_cast<cs_packet_attack*>(g_clients[id].m_packet_start);
		process_attack(id);
		break;
	}
	default: cout << "Unknown Packet type [" << p_type << "] from Client [" << id << "]\n";
		while (true);
	}
}

constexpr int MIN_BUFF_SIZE = 1024;

void process_recv(int id, DWORD iosize)
{
	unsigned char p_size = g_clients[id].m_packet_start[0];
	unsigned char* next_recv_ptr = g_clients[id].m_recv_start + iosize;
	while (p_size <= next_recv_ptr - g_clients[id].m_packet_start) {
		process_packet(id);
		g_clients[id].m_packet_start += p_size;
		if (g_clients[id].m_packet_start < next_recv_ptr)
			p_size = g_clients[id].m_packet_start[0];
		else break;
	}

	long long left_data = next_recv_ptr - g_clients[id].m_packet_start;

	if ((MAX_BUFFER - (next_recv_ptr - g_clients[id].m_recv_over.iocp_buf))
		< MIN_BUFF_SIZE) {
		memcpy(g_clients[id].m_recv_over.iocp_buf,
			g_clients[id].m_packet_start, left_data);
		g_clients[id].m_packet_start = g_clients[id].m_recv_over.iocp_buf;
		next_recv_ptr = g_clients[id].m_packet_start + left_data;
	}
	DWORD recv_flag = 0;
	g_clients[id].m_recv_start = next_recv_ptr;
	g_clients[id].m_recv_over.wsa_buf.buf = reinterpret_cast<CHAR*>(next_recv_ptr);
	g_clients[id].m_recv_over.wsa_buf.len = MAX_BUFFER -
		static_cast<int>(next_recv_ptr - g_clients[id].m_recv_over.iocp_buf);

	g_clients[id].c_lock.lock();
	if (true == g_clients[id].in_use) {
		WSARecv(g_clients[id].m_sock, &g_clients[id].m_recv_over.wsa_buf,
			1, NULL, &recv_flag, &g_clients[id].m_recv_over.wsa_over, NULL);
	}
	g_clients[id].c_lock.unlock();
}

void add_new_client(SOCKET ns)
{
	int i;
	id_lock.lock();
	for (i = 0; i < MAX_USER; ++i)
		if (false == g_clients[i].in_use) break;
	id_lock.unlock();
	if (MAX_USER == i) {
		cout << "Max user limit exceeded.\n";
		closesocket(ns);
	}
	else {
		// cout << "New Client [" << i << "] Accepted" << endl;
		g_clients[i].c_lock.lock();
		g_clients[i].in_use = true;
		g_clients[i].m_sock = ns;
		g_clients[i].name[0] = 0;
		g_clients[i].level = 1;
		g_clients[i].hp = 10;
		g_clients[i].exp = 0;
		g_clients[i].can_attack = true;
		g_clients[i].c_lock.unlock();

		g_clients[i].view_list.clear();

		g_clients[i].m_packet_start = g_clients[i].m_recv_over.iocp_buf;
		g_clients[i].m_recv_over.op_mode = OP_MODE_RECV;
		g_clients[i].m_recv_over.wsa_buf.buf
			= reinterpret_cast<CHAR*>(g_clients[i].m_recv_over.iocp_buf);
		g_clients[i].m_recv_over.wsa_buf.len = sizeof(g_clients[i].m_recv_over.iocp_buf);
		ZeroMemory(&g_clients[i].m_recv_over.wsa_over, sizeof(g_clients[i].m_recv_over.wsa_over));
		g_clients[i].m_recv_start = g_clients[i].m_recv_over.iocp_buf;

		g_clients[i].x = rand() % WORLD_WIDTH;
		g_clients[i].y = rand() % WORLD_HEIGHT;

		int sec_idx_x = get_sector_x_index(g_clients[i].x);
		int sec_idx_y = get_sector_y_index(g_clients[i].y);
		g_clients[i].sec_x = sec_idx_x;
		g_clients[i].sec_y = sec_idx_y;
		insert_obj_in_sector(i, g_clients[i].sec_x, g_clients[i].sec_y);

		CreateIoCompletionPort(reinterpret_cast<HANDLE>(ns), h_iocp, i, 0);
		DWORD flags = 0;
		int ret;
		g_clients[i].c_lock.lock();
		if (true == g_clients[i].in_use) {
			ret = WSARecv(g_clients[i].m_sock, &g_clients[i].m_recv_over.wsa_buf, 1, NULL,
				&flags, &g_clients[i].m_recv_over.wsa_over, NULL);
		}
		g_clients[i].c_lock.unlock();
		if (SOCKET_ERROR == ret) {
			int err_no = WSAGetLastError();
			if (ERROR_IO_PENDING != err_no)
				error_display("WSARecv : ", err_no);
		}
	}
	SOCKET cSocket = WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);
	g_accept_over.op_mode = OP_MODE_ACCEPT;
	g_accept_over.wsa_buf.len = static_cast<ULONG> (cSocket);
	ZeroMemory(&g_accept_over.wsa_over, sizeof(&g_accept_over.wsa_over));
	AcceptEx(g_lSocket, cSocket, g_accept_over.iocp_buf, 0, 32, 32, NULL, &g_accept_over.wsa_over);
}

void disconnect_client(int id)
{
	for (int i = 0; i < MAX_USER; ++i) {
		if (true == g_clients[i].in_use)
			if (i != id) {
				g_clients[id].vl.lock();
				if (0 != g_clients[i].view_list.count(id)) {
					g_clients[i].view_list.erase(id);
					send_leave_packet(i, id);
				}
				g_clients[id].vl.unlock();
			}
	}

	erase_obj_in_sector(id, g_clients[id].sec_x, g_clients[id].sec_y);

	g_clients[id].c_lock.lock();
	g_clients[id].in_use = false;
	g_clients[id].vl.lock();
	g_clients[id].view_list.clear();
	g_clients[id].vl.unlock();
	closesocket(g_clients[id].m_sock);
	g_clients[id].m_sock = 0;
	g_clients[id].c_lock.unlock();
}

void resurrection_npc(int id)
{
	g_clients[id].c_lock.lock();
	g_clients[id].in_use = true;

	g_clients[id].hp = 10;
	g_clients[id].level = 1;

	g_clients[id].overlapped = false;
	g_clients[id].move_count = 0;
	g_clients[id].c_lock.unlock();

	int sec_idx_x = get_sector_x_index(g_clients[id].x);
	int sec_idx_y = get_sector_y_index(g_clients[id].y);
	g_clients[id].sec_x = sec_idx_x;
	g_clients[id].sec_y = sec_idx_y;
	insert_obj_in_sector(id, g_clients[id].sec_x, g_clients[id].sec_y);

	random_move_npc(id);
}

void worker_thread()
{
	// 반복
	//   - 이 쓰레드를 IOCP thread pool에 등록  => GQCS
	//   - iocp가 처리를 맞긴 I/O완료 데이터를 꺼내기 => GQCS
	//   - 꺼낸 I/O완료 데이터를 처리
	while (true) {
		DWORD io_size;
		int key;
		ULONG_PTR iocp_key;
		WSAOVERLAPPED* lpover;
		int ret = GetQueuedCompletionStatus(h_iocp, &io_size, &iocp_key, &lpover, INFINITE);
		key = static_cast<int>(iocp_key);
		if (FALSE == ret) {
			error_display("GQCS Error : ", WSAGetLastError());
		}

		OVER_EX* over_ex = reinterpret_cast<OVER_EX*>(lpover);
		switch (over_ex->op_mode) {
		case OP_MODE_ACCEPT:
			add_new_client(static_cast<SOCKET>(over_ex->wsa_buf.len));
			break;
		case OP_MODE_RECV:
			if (0 == io_size)
				disconnect_client(key);
			else {
				process_recv(key, io_size);
			}
			break;
		case OP_MODE_SEND:
		{
			delete over_ex;
		}
		break;
		case OP_RANDOM_MOVE:
		{
			random_move_npc(key);
			delete over_ex;
		}
		break;
		case OP_ATTACK:
		{
			g_clients[key].c_lock.lock();
			g_clients[key].can_attack = true;
			g_clients[key].c_lock.unlock();
			delete over_ex;
		}
		break;
		case OP_RESURRECTION:
		{
			resurrection_npc(key);
			delete over_ex;
		}
		break;
		case OP_PLAYER_MOVE_NOTIFY:
		{
			g_clients[key].lua_l.lock();
			lua_getglobal(g_clients[key].L, "event_player_move");
			lua_pushnumber(g_clients[key].L, over_ex->object_id);
			lua_pcall(g_clients[key].L, 1, 0, 0);
			g_clients[key].lua_l.unlock();
			delete over_ex;
		}
		break;
		case OP_MOVE_COUNT:
		{
			g_clients[key].lua_l.lock();
			lua_getglobal(g_clients[key].L, "check_move_count");
			lua_pushnumber(g_clients[key].L, over_ex->object_id);
			lua_pcall(g_clients[key].L, 1, 0, 0);
			g_clients[key].lua_l.unlock();
			delete over_ex;
		}
		break;
		case OP_HP_HEALING:
		{
			process_Healing(key);
			delete over_ex;
		}
		break;
		}
	}
}

int API_get_x(lua_State* L)
{
	int user_id = lua_tointeger(L, -1);
	int x = g_clients[user_id].x;
	lua_pushnumber(L, x);
	lua_pop(L, 2);
	return 1;
}

int API_get_y(lua_State* L)
{
	int user_id = lua_tointeger(L, -1);
	lua_pop(L, 1);
	int y = g_clients[user_id].y;
	lua_pushnumber(L, y);
	lua_pop(L, 2);
	return 1;
}

int API_SendMessage(lua_State* L)
{
	int my_id = (int)lua_tointeger(L, -3);
	int user_id = (int)lua_tointeger(L, -2);
	char* mess = (char*)lua_tostring(L, -1);

	lua_pop(L, 3);

	send_chat_packet(user_id, my_id, mess);
	return 0;
}

int API_get_overlapped(lua_State* L)
{
	int user_id = lua_tointeger(L, -1);
	bool overlapped = g_clients[user_id].overlapped;
	lua_pushnumber(L, overlapped);
	lua_pop(L, 2);
	return 1;
}

int API_get_move_count(lua_State* L)
{
	int user_id = lua_tointeger(L, -1);
	lua_pop(L, 1);
	int move_count = g_clients[user_id].move_count;
	lua_pushnumber(L, move_count);
	return 1;
}

int API_set_overlapped(lua_State* L)
{
	int my_id = (int)lua_tointeger(L, -2);
	bool over = (bool)lua_toboolean(L, -1);

	lua_pop(L, 2);

	g_clients[my_id].overlapped = over;
	return 0;
}

int API_set_move_count(lua_State* L)
{
	int my_id = (int)lua_tointeger(L, -2);
	int move_count = (int)lua_tointeger(L, -1);

	lua_pop(L, 2);

	g_clients[my_id].move_count = move_count;
	return 0;
}

void initialize_NPC()
{
	cout << "Initializing NPCs\n";
	for (int i = MAX_USER; i < MAX_USER + NUM_NPC; ++i)
	{
		g_clients[i].x = rand() % WORLD_WIDTH;
		g_clients[i].y = rand() % WORLD_HEIGHT;

		char npc_name[50];
		sprintf_s(npc_name, "N%d", i);
		strcpy_s(g_clients[i].name, npc_name);
		g_clients[i].is_active = false;
		g_clients[i].in_use = true;

		g_clients[i].hp = 10;
		g_clients[i].level = 1;

		g_clients[i].overlapped = false;
		g_clients[i].move_count = 0;

		int sec_idx_x = get_sector_x_index(g_clients[i].x);
		int sec_idx_y = get_sector_y_index(g_clients[i].y);
		g_clients[i].sec_x = sec_idx_x;
		g_clients[i].sec_y = sec_idx_y;
		insert_obj_in_sector(i, g_clients[i].sec_x, g_clients[i].sec_y);

		lua_State* L = g_clients[i].L = luaL_newstate();
		luaL_openlibs(L);

		int error = luaL_loadfile(L, "monster.lua");
		error = lua_pcall(L, 0, 0, 0);

		lua_getglobal(L, "set_uid");
		lua_pushnumber(L, i);
		lua_pcall(L, 1, 1, 0);
		// lua_pop(L, 1);// eliminate set_uid from stack after call

		lua_register(L, "API_SendMessage", API_SendMessage);
		lua_register(L, "API_get_x", API_get_x);
		lua_register(L, "API_get_y", API_get_y);
		lua_register(L, "API_get_overlapped", API_get_overlapped);
		lua_register(L, "API_get_move_count", API_get_move_count);
		lua_register(L, "API_set_overlapped", API_set_overlapped);
		lua_register(L, "API_set_move_count", API_set_move_count);
	}
	cout << "NPC initialize finished.\n";
}

void random_move_npc(int id)
{
	if (false == g_clients[id].in_use) return;

	unordered_set <int> old_viewlist;
	unordered_set <int> *obj_list = new unordered_set<int>;
	get_obj_list_in_near_sector(obj_list, g_clients[id].sec_x, g_clients[id].sec_y);
	
	for (const auto& p : *obj_list)
	{
		if (false == g_clients[p].in_use) continue;
		if(false == is_npc(p))
			if (true == is_near(id, p)) old_viewlist.insert(p);
	}

	int x = g_clients[id].x;
	int y = g_clients[id].y;
	switch (rand() % 4)
	{
	case 0: if (x > 0) x--; break;
	case 1: if (x < (WORLD_WIDTH - 1)) x++; break;
	case 2: if (y > 0) y--; break;
	case 3: if (y < (WORLD_HEIGHT - 1)) y++; break;
	}
	g_clients[id].x = x;
	g_clients[id].y = y;

	int sec_x = get_sector_x_index(g_clients[id].x);
	int sec_y = get_sector_y_index(g_clients[id].y);

	if (sec_x != g_clients[id].sec_x || sec_y != g_clients[id].sec_y)
	{
		g_clients[id].sec_x = sec_x;
		g_clients[id].sec_y = sec_y;
		erase_obj_in_sector(id, g_clients[id].sec_x, g_clients[id].sec_y);
		insert_obj_in_sector(id, g_clients[id].sec_x, g_clients[id].sec_y);
	}

	unordered_set <int> new_viewlist;

	get_obj_list_in_near_sector(obj_list, g_clients[id].sec_x, g_clients[id].sec_y);

	for (const auto p : *obj_list) {
		if (id == p) continue;
		if (false == g_clients[p].in_use) continue;
		if (false == is_npc(p))
			if (true == is_near(id, p)) new_viewlist.insert(p);
	}

	for (auto pl : old_viewlist) {
		if (0 < new_viewlist.count(pl)) {
			if (0 < g_clients[pl].view_list.count(id))
				send_move_packet(pl, id);
			else {
				g_clients[pl].vl.lock();
				g_clients[pl].view_list.insert(id);
				g_clients[pl].vl.unlock();
				send_enter_packet(pl, id);
			}
		}
		else
		{
			if (0 < g_clients[pl].view_list.count(id)) {
				g_clients[pl].vl.lock();
				g_clients[pl].view_list.erase(id);
				g_clients[pl].vl.unlock();
				send_leave_packet(pl, id);
			}
		}
	}

	for (auto pl : new_viewlist) {
		if (0 == g_clients[pl].view_list.count(pl)) {
			if (0 == g_clients[pl].view_list.count(id)) {
				g_clients[pl].vl.lock();
				g_clients[pl].view_list.insert(id);
				g_clients[pl].vl.unlock();
				send_enter_packet(pl, id);
			}
			else
				send_move_packet(pl, id);
		}
	}

	if (true == new_viewlist.empty()) {
		//g_clients[id].is_active = false;
		CAS(&g_clients[id].is_active, true, false);
	}
	else {
		if(true == g_clients[id].in_use)
			add_timer(id, OP_RANDOM_MOVE, system_clock::now() + 1s);
	}

	for (auto pc : new_viewlist)
	{
		OVER_EX* over_ex = new OVER_EX;
		over_ex->object_id = pc;
		over_ex->op_mode = OP_MOVE_COUNT;
		PostQueuedCompletionStatus(h_iocp, 1, id, &over_ex->wsa_over);
	}

	for (auto pc : new_viewlist) {
		OVER_EX* over_ex = new OVER_EX;
		over_ex->object_id = pc;
		over_ex->op_mode = OP_PLAYER_MOVE_NOTIFY;
		PostQueuedCompletionStatus(h_iocp, 1, id, &over_ex->wsa_over);
	}

	delete obj_list;
}

int main()
{
	std::wcout.imbue(std::locale("korean"));
	for (auto& cl : g_clients)
		cl.in_use = false;

	WSADATA WSAData;
	WSAStartup(MAKEWORD(2, 0), &WSAData);
	h_iocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, NULL, 0);
	g_lSocket = WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);
	CreateIoCompletionPort(reinterpret_cast<HANDLE>(g_lSocket), h_iocp, KEY_SERVER, 0);

	SOCKADDR_IN serverAddr;
	memset(&serverAddr, 0, sizeof(SOCKADDR_IN));
	serverAddr.sin_family = AF_INET;
	serverAddr.sin_port = htons(SERVER_PORT);
	serverAddr.sin_addr.s_addr = INADDR_ANY;
	::bind(g_lSocket, (sockaddr*)&serverAddr, sizeof(serverAddr));
	listen(g_lSocket, 5);

	SOCKET cSocket = WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);
	g_accept_over.op_mode = OP_MODE_ACCEPT;
	g_accept_over.wsa_buf.len = static_cast<int>(cSocket);
	ZeroMemory(&g_accept_over.wsa_over, sizeof(&g_accept_over.wsa_over));
	AcceptEx(g_lSocket, cSocket, g_accept_over.iocp_buf, 0, 32, 32, NULL, &g_accept_over.wsa_over);

	initialize_NPC();

	//thread ai_thread{ npc_ai_thread };
	thread timer_thread{ time_worker };
	vector <thread> worker_threads;
	for (int i = 0; i < 6; ++i)
		worker_threads.emplace_back(worker_thread);
	for (auto& th : worker_threads)
		th.join();
	timer_thread.join();

	//ai_thread.join();
	closesocket(g_lSocket);
	WSACleanup();
}
