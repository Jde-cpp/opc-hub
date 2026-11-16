create or alter proc [dbo].gateway_oee_floor_purge( @oee_floor_id int ) as begin
	set xact_abort on;
	begin tran;
	delete from gateway_oee_floor_shapes where oee_floor_id = @oee_floor_id;
	delete from gateway_oee_floors where oee_floor_id = @oee_floor_id;
	commit;
end