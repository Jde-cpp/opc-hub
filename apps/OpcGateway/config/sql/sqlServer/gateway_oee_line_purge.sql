create or alter proc [dbo].gateway_oee_line_purge( @oee_line_id int ) as begin
	set xact_abort on;
	begin tran;
	delete from gateway_oee_floor_shapes where member_line_id = @oee_line_id or oee_floor_id in ( select oee_floor_id from gateway_oee_floors where oee_line_id = @oee_line_id );
	delete from gateway_oee_floors where oee_line_id = @oee_line_id;
	delete from gateway_oee_line_members where oee_line_id = @oee_line_id or member_line_id = @oee_line_id;
	delete from gateway_oee_lines where oee_line_id = @oee_line_id;
	commit;
end