create or alter proc [dbo].gateway_flow_purge( @flow_id int ) as begin
	set xact_abort on;
	begin tran;
	delete from gateway_flow_contexts where flow_id = @flow_id;
	delete from gateway_flow_runs where flow_id = @flow_id;
	delete from gateway_flow_versions where flow_id = @flow_id;
	delete from gateway_flows where flow_id = @flow_id;
	commit;
end