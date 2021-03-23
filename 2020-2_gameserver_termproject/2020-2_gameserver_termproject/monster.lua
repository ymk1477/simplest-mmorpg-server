myid = 99999;

function set_uid( x )
	myid = x;
end

function event_player_move(player)
	local player_x = API_get_x (player);
	local player_y = API_get_y (player);
	local my_x = API_get_x (myid);
	local my_y = API_get_y (myid);
	local overlapped = API_get_overlapped(myid);

	if(overlapped == false) then
		if(player_x == my_x ) then
			if(player_y == my_y) then
				API_set_overlapped(myid, true);
				API_set_move_count(myid, 0);
				API_SendMessage(myid, player, "HELLO");
			end
		end
	else
		if(player_x == my_x ) then
			if(player_y == my_y) then
				API_set_overlapped(myid, true);
				API_set_move_count(myid, 0);
				API_SendMessage(myid, player, "HELLO");
			end
		end
	end	
end

function check_move_count(player)
	local player_x = API_get_x (player);
	local player_y = API_get_y (player);
	local my_x = API_get_x (myid);
	local my_y = API_get_y (myid);
	local overlapped = API_get_overlapped(myid);
	local move_count = API_get_move_count(myid);

	if(overlapped == false) then
		if(player_x == my_x ) then
			if(player_y == my_y) then
				API_set_overlapped(myid, true);
				API_set_move_count(myid, 0);
			end
		end
	else
		if(player_x == my_x ) then
			if(player_y == my_y) then
				API_set_overlapped(myid, true);
				API_set_move_count(myid, 0);
			else
				API_set_move_count(myid, move_count + 1);
			end
		else
			API_set_move_count(myid, move_count + 1);
		end
	end	

	overlapped = API_get_overlapped(myid);
	move_count = API_get_move_count(myid);

	if( overlapped == 1 and move_count == 3) then
		
		API_set_overlapped(myid, false);
		API_set_move_count(myid, 0);
		API_SendMessage(myid, player, "BYE");
	end
end
