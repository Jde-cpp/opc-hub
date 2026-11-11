drop procedure if exists gateway_oee_line_purge;
go

create procedure gateway_oee_line_purge( _oee_line_id int unsigned )
begin
	declare exit handler for sqlexception begin rollback; resignal; end;
	start transaction; -- the commit is the last statement of the call, so it reports 0 rows and the purge is not published (PurgeAwait, #47).
	delete from gateway_oee_line_members where oee_line_id = _oee_line_id or member_line_id = _oee_line_id;
	delete from gateway_oee_lines where oee_line_id = _oee_line_id;
	commit;
end