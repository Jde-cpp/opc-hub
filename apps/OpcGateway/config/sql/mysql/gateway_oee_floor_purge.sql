drop procedure if exists gateway_oee_floor_purge;
go

create procedure gateway_oee_floor_purge( _oee_floor_id int unsigned )
begin
	declare exit handler for sqlexception begin rollback; resignal; end;
	start transaction; -- the commit is the last statement of the call, so it reports 0 rows and the purge is not published (PurgeAwait, #47).
	delete from gateway_oee_floor_shapes where oee_floor_id = _oee_floor_id;
	delete from gateway_oee_floors where oee_floor_id = _oee_floor_id;
	commit;
end