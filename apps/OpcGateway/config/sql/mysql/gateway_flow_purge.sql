drop procedure if exists gateway_flow_purge;
go

create procedure gateway_flow_purge( _flow_id int unsigned )
begin
	declare exit handler for sqlexception begin rollback; resignal; end;
	start transaction; -- the commit is the last statement of the call, so it reports 0 rows and the purge is not published (PurgeAwait, #47).
	delete from gateway_flow_contexts where flow_id = _flow_id;
	delete from gateway_flow_runs where flow_id = _flow_id;
	delete from gateway_flow_versions where flow_id = _flow_id;
	delete from gateway_flows where flow_id = _flow_id;
	commit;
end