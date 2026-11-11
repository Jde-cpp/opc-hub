drop procedure if exists gateway_oee_machine_purge;
go

create procedure gateway_oee_machine_purge( _oee_machine_id int unsigned )
begin
	declare exit handler for sqlexception begin rollback; resignal; end;
	start transaction; -- the commit is the last statement of the call, so it reports 0 rows and the purge is not published (PurgeAwait, #47).
	delete from gateway_oee_intervals where oee_machine_id = _oee_machine_id;
	delete from gateway_oee_stops where oee_machine_id = _oee_machine_id;
	delete from gateway_oee_codings where oee_machine_id = _oee_machine_id;
	delete from gateway_oee_machine_signals where oee_machine_id = _oee_machine_id;
	delete from gateway_oee_line_members where oee_machine_id = _oee_machine_id;
	delete from gateway_oee_machines where oee_machine_id = _oee_machine_id;
	commit;
end