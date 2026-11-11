create or alter proc [dbo].gateway_oee_machine_purge( @oee_machine_id int ) as begin
	set xact_abort on;
	begin tran;
	delete from gateway_oee_intervals where oee_machine_id = @oee_machine_id;
	delete from gateway_oee_stops where oee_machine_id = @oee_machine_id;
	delete from gateway_oee_codings where oee_machine_id = @oee_machine_id;
	delete from gateway_oee_machine_signals where oee_machine_id = @oee_machine_id;
	delete from gateway_oee_line_members where oee_machine_id = @oee_machine_id;
	delete from gateway_oee_machines where oee_machine_id = @oee_machine_id;
	commit;
end