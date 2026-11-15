//Demo phase 3 (#480):  the floor's two tables (docs/demo/spec.md *The floor*) over the pumps rig - a floor on an area over two
//lines, pump1's and pump2's, a shape of each kind made through the QL and read back, each refusal of the rules a BadRequest, a
//deleted machine's shape, the purges and the unique index.
#include "utils/OeeFixture.h"

namespace Jde::Opc::Gateway::Tests{
	struct OeeFloorTests : OeeFixture{
		Ω CreateFloor( uint32 line, jobject args={} )ε->jobject{
			string ql{ "createOeeFloor( oeeLineId:$line" };
			jobject vars{ {"line", line} };
			for( let& [key,value] : args ){
				ql += Ƒ( ", {}:${}", key, key );
				vars[key] = value;
			}
			return Mutate( ql+" ){ id width height grid }", move(vars) ).as_object();
		}
		Ω CreateShape( uint32 floor, sv kind, jvalue points, jobject args={} )ε->uint32{
			string ql{ "createOeeFloorShape( oeeFloorId:$floor, kind:$kind, points:$points" };
			jobject vars{ {"floor", floor}, {"kind", kind}, {"points", move(points)} };
			for( let& [key,value] : args ){
				ql += Ƒ( ", {}:${}", key, key );
				vars[key] = value;
			}
			return Id( Mutate(ql+" ){ id }", move(vars)) );
		}
		Ω Count( sv table, sv column, uint32 id )ε->uint{
			let rows = DS()->Select( {Ƒ("select count(*) from {} where {}=?", GetViewPtr(table)->SqlName(), column), {DB::Value{(uint)id}}} );
			return rows.front().Get<uint>( 0 );
		}
		Ω Shapes( uint32 floor )ε->uint{ return Count( OeeTable::FloorShapes, "oee_floor_id", floor ); }
		Ω Floors( uint32 line )ε->uint{ return Count( OeeTable::Floors, "oee_line_id", line ); }
		Ω Box( double x, double y, double w, double h )ι->jarray{ return { jarray{x, y}, jarray{x+w, y}, jarray{x+w, y+h}, jarray{x, y+h} }; }
	};

	//The two tables synced;  the plant's floor, its refusals first, then a shape of each kind read back;  each refusal of the
	//shapes' rules;  a deleted machine's shape read;  a machine's purge taking its shape and leaving the floor, a line's taking
	//its floor with its shapes and its Line shape, a floor's taking its shapes;  and a second floor on a line refused by the index.
	TEST_F( OeeFloorTests, TheFloorAndItsShapes ){
		for( sv table : {"oeeFloors", "oeeFloorShapes"} )
			EXPECT_NO_THROW( Mutate(Ƒ("{}{{ id }}", table)) ) << table;
		let from = floor<seconds>( Now() )-5s;
		Pump1Machine pump1{ "oeeFloor1", from-2s };
		Pump1Machine pump2{ "oeeFloor2", from-2s, true, 100, 5, Pump2Nodes };
		let m1 = pump1.Create( from, {} );
		let m2 = pump2.Create( from, {} );
		Lines lines;
		let plant = lines.Create( "oeeFloorPlant", "Area" );
		let bottling = lines.Create( "oeeFloorBottling", "Line" );
		let packing = lines.Create( "oeeFloorPacking", "Line" );
		AddLine( plant, bottling, 1 );
		AddLine( plant, packing, 2 );
		AddMachine( bottling, m1 );
		AddMachine( packing, m2 );

		EXPECT_EQ( Status([&]{ CreateFloor( 999'999, {{"width", 40}, {"height", 20}} ); }), EHttpStatus::NotFound );
		EXPECT_NE( Refused([&]{ CreateFloor( plant, {{"height", 20}} ); }).find("width is metres"), string::npos ) << "required";
		EXPECT_NE( Refused([&]{ CreateFloor( plant, {{"width", 0}, {"height", 20}} ); }).find("width must be above zero"), string::npos );
		EXPECT_NE( Refused([&]{ CreateFloor( plant, {{"width", 40}, {"height", "20"}} ); }).find("height is metres"), string::npos );
		EXPECT_NE( Refused([&]{ CreateFloor( plant, {{"width", 40}, {"height", 20}, {"grid", 0}} ); }).find("grid must be above zero"), string::npos );
		Mutate( "deleteOeeLine( id:$id )", {{"id", packing}} );
		EXPECT_NE( Refused([&]{ CreateFloor( packing, {{"width", 40}, {"height", 20}} ); }).find("deleted"), string::npos );
		Mutate( "restoreOeeLine( id:$id )", {{"id", packing}} );
		EXPECT_EQ( Floors(plant), 0u ) << "a refused create made no floor";
		let hall = CreateFloor( plant, {{"width", 40}, {"height", 20}} );
		let floorId = Json::AsNumber<uint32>( hall, "id" );
		EXPECT_EQ( Number(hall, "width"), 40 );
		EXPECT_NE( Refused([&]{ CreateFloor( plant, {{"width", 10}, {"height", 10}} ); }).find("has a floor"), string::npos );
		EXPECT_ANY_THROW( DS()->ExecuteSync({Ƒ("insert into {}( oee_line_id, width, height, grid ) values( ?, 1, 1, 1 )", GetViewPtr(OeeTable::Floors)->SqlName()), {DB::Value{(uint)plant}}}) ) << "the unique index, the backstop";
		EXPECT_EQ( Floors(plant), 1u );
		EXPECT_NE( Refused([&]{ Mutate( "deleteOeeFloor( id:$id )", {{"id", floorId}} ); }).find("purge"), string::npos );
		EXPECT_EQ( Status([&]{ Mutate( "updateOeeFloor( id:$id, grid:1 )", {{"id", 999'999}} ); }), EHttpStatus::NotFound );

		//A shape of each kind, the wall's points as text, the rest as lists, read back as the column holds them.
		let filler = CreateShape( floorId, "Machine", Box(3, 3, 6, 4), {{"oeeMachineId", m1}} );
		let bottlingShape = CreateShape( floorId, "Line", Box(1, 1, 19, 8), {{"memberLineId", bottling}, {"label", "Bottling"}} );
		let conveyor = CreateShape( floorId, "Zone", Box(9, 4.5, 4, 1), {{"label", "Conveyor"}} );
		let wall = CreateShape( floorId, "Wall", "[[0,0],[40,0],[40,20],[0,20],[0,0]]" );
		let aisle = CreateShape( floorId, "Aisle", jarray{jarray{0, 10}, jarray{40, 10}} );
		let dock = CreateShape( floorId, "Label", jarray{jarray{38, 15}}, {{"label", "Dock"}, {"sort", 2}} );
		let rows = Mutate( "oeeFloorShapes( oeeFloorId:$floor ){ id kind oeeMachineId memberLineId points label sort }", {{"floor", floorId}} ).as_array();
		ASSERT_EQ( rows.size(), 6u ) << serialize( rows );
		let shape = [&]( uint32 id )->const jobject&{
			let p = std::ranges::find_if( rows, [id]( const jvalue& v ){ return Json::AsNumber<uint32>(v.as_object(), "id")==id; } );
			THROW_IF( p==rows.end(), "Shape {} wasn't read back.", id );
			return p->as_object();
		};
		EXPECT_EQ( Json::AsSV(shape(filler), "kind"), "Machine" );
		EXPECT_EQ( Json::AsNumber<uint32>(shape(filler), "oeeMachineId"), m1 );
		EXPECT_TRUE( shape(filler).at("memberLineId").is_null() );
		EXPECT_EQ( Json::AsSV(shape(filler), "points"), "[[3,3],[9,3],[9,7],[3,7]]" ) << "a list sent is spelled as text";
		EXPECT_TRUE( shape(filler).at("label").is_null() );
		EXPECT_EQ( Number(shape(filler), "sort"), 0 );
		EXPECT_EQ( Json::AsNumber<uint32>(shape(bottlingShape), "memberLineId"), bottling );
		EXPECT_EQ( Json::AsSV(shape(bottlingShape), "label"), "Bottling" );
		EXPECT_EQ( Json::AsSV(shape(conveyor), "points"), "[[9,4.5],[13,4.5],[13,5.5],[9,5.5]]" );
		EXPECT_EQ( Json::AsSV(shape(wall), "points"), "[[0,0],[40,0],[40,20],[0,20],[0,0]]" ) << "text sent is kept";
		EXPECT_EQ( Json::AsSV(shape(aisle), "kind"), "Aisle" );
		EXPECT_EQ( Json::AsSV(shape(dock), "label"), "Dock" );
		EXPECT_EQ( Number(shape(dock), "sort"), 2 );
		let read = Mutate( "oeeFloors( id:$id ){ id oeeLineId width height grid }", {{"id", floorId}} ).as_array();
		ASSERT_EQ( read.size(), 1u );
		EXPECT_EQ( Json::AsNumber<uint32>(read[0].as_object(), "oeeLineId"), plant );
		EXPECT_EQ( Number(read[0].as_object(), "grid"), 0.5 ) << "the default";

		//The shapes' refusals, each a BadRequest that writes nothing.
		let bottlingHall = CreateFloor( bottling, {{"width", 20}, {"height", 10}} );
		let bottlingFloor = Json::AsNumber<uint32>( bottlingHall, "id" );
		EXPECT_NE( Refused([&]{ CreateShape( bottlingFloor, "Machine", Box(1, 1, 2, 2), {{"oeeMachineId", m2}} ); }).find("isn't one line 'oeeFloorBottling' reaches"), string::npos ) << "a machine the line does not reach";
		EXPECT_NE( Refused([&]{ CreateShape( bottlingFloor, "Line", Box(1, 1, 2, 2), {{"memberLineId", packing}} ); }).find("isn't one line 'oeeFloorBottling' reaches"), string::npos );
		EXPECT_EQ( Status([&]{ CreateShape( bottlingFloor, "Machine", Box(1, 1, 2, 2), {{"oeeMachineId", 999'999}} ); }), EHttpStatus::NotFound );
		EXPECT_EQ( Status([&]{ CreateShape( bottlingFloor, "Line", Box(1, 1, 2, 2), {{"memberLineId", 999'999}} ); }), EHttpStatus::NotFound );
		EXPECT_EQ( Status([&]{ CreateShape( 999'999, "Label", jarray{jarray{1, 1}} ); }), EHttpStatus::NotFound );
		EXPECT_NE( Refused([&]{ CreateShape( floorId, "Machine", Box(20, 3, 6, 4), {{"oeeMachineId", m1}} ); }).find("is on the floor already"), string::npos ) << "a second shape for one machine";
		EXPECT_NE( Refused([&]{ CreateShape( floorId, "Line", Box(20, 3, 6, 4), {{"memberLineId", bottling}} ); }).find("is on the floor already"), string::npos );
		EXPECT_NE( Refused([&]{ CreateShape( floorId, "Zone", Box(39, 19, 2, 2) ); }).find("[41, 19] is outside the floor, 40 by 20"), string::npos ) << "a point outside the floor";
		EXPECT_NE( Refused([&]{ CreateShape( floorId, "Machine", jarray{jarray{20, 3}, jarray{26, 7}}, {{"oeeMachineId", m2}} ); }).find("closed polygon of three points or more, not 2"), string::npos ) << "a Machine shape of two points";
		EXPECT_NE( Refused([&]{ CreateShape( floorId, "Label", jarray{jarray{20, 3}, jarray{26, 7}} ); }).find("A Label is one point, not 2"), string::npos ) << "a Label of two";
		EXPECT_NE( Refused([&]{ CreateShape( floorId, "Wall", jarray{jarray{20, 3}} ); }).find("polyline of two points or more, not 1"), string::npos );
		EXPECT_NE( Refused([&]{ CreateShape( floorId, "Line", Box(20, 3, 6, 4), {{"memberLineId", plant}} ); }).find("is the floor's own"), string::npos ) << "the floor's own line as a Line shape";
		EXPECT_NE( Refused([&]{ CreateShape( floorId, "Box", Box(20, 3, 6, 4) ); }).find("kind is Machine, Line, Zone, Wall, Aisle or Label, not \"Box\""), string::npos ) << "a kind outside the six";
		EXPECT_NE( Refused([&]{ CreateShape( floorId, "Machine", Box(20, 3, 6, 4), {{"oeeMachineId", m2}, {"memberLineId", packing}} ); }).find("not a line"), string::npos ) << "memberLineId on the wrong kind";
		EXPECT_NE( Refused([&]{ CreateShape( floorId, "Zone", Box(20, 3, 6, 4), {{"oeeMachineId", m2}} ); }).find("names no machine or line"), string::npos ) << "oeeMachineId on the wrong kind";
		EXPECT_NE( Refused([&]{ CreateShape( floorId, "Line", Box(20, 3, 6, 4) ); }).find("memberLineId is required"), string::npos );
		EXPECT_NE( Refused([&]{ CreateShape( floorId, "Label", "not json" ); }).find("points must be"), string::npos );
		EXPECT_NE( Refused([&]{ Mutate( "createOeeFloorShape( oeeFloorId:$floor, kind:\"Label\" ){ id }", {{"floor", floorId}} ); }).find("points"), string::npos ) << "required";
		EXPECT_EQ( Shapes(floorId), 6u ) << "nothing was written";
		EXPECT_EQ( Shapes(bottlingFloor), 0u );
		//An update:  the points it moves to are held to the floor, a floor shrinks no further than its shapes, and a shape has no deleted.
		EXPECT_NE( Refused([&]{ Mutate( "updateOeeFloorShape( id:$id, points:$points )", {{"id", dock}, {"points", jarray{jarray{38, 21}}}} ); }).find("outside"), string::npos );
		EXPECT_NO_THROW( Mutate("updateOeeFloorShape( id:$id, points:$points, label:$label )", {{"id", dock}, {"points", jarray{jarray{39, 16}}}, {"label", "Loading dock"}}) );
		EXPECT_EQ( Json::AsSV(Mutate("oeeFloorShapes( id:$id ){ points label }", {{"id", dock}}).as_array()[0].as_object(), "points"), "[[39,16]]" );
		EXPECT_NE( Refused([&]{ Mutate( "updateOeeFloor( id:$id, width:8 )", {{"id", floorId}} ); }).find("[9, 3] is outside the floor, 8 by 20"), string::npos );
		EXPECT_NO_THROW( Mutate("updateOeeFloor( id:$id, grid:1 )", {{"id", floorId}}) );
		EXPECT_EQ( Number(Mutate("oeeFloors( id:$id ){ grid }", {{"id", floorId}}).as_array()[0].as_object(), "grid"), 1 );
		EXPECT_NE( Refused([&]{ Mutate( "deleteOeeFloorShape( id:$id )", {{"id", dock}} ); }).find("purge"), string::npos );
		EXPECT_EQ( Status([&]{ Mutate( "purgeOeeFloorShape( id:$id )", {{"id", 999'999}} ); }), EHttpStatus::NotFound );

		//A deleted machine keeps its shape, which reads.
		let packer = CreateShape( floorId, "Machine", Box(13, 13, 4, 4), {{"oeeMachineId", m2}} );
		Mutate( "deleteOeeMachine( id:$id )", {{"id", m2}} );
		EXPECT_EQ( Count(OeeTable::FloorShapes, "oee_machine_id", m2), 1u );
		EXPECT_EQ( Mutate("oeeFloorShapes( id:$id ){ id }", {{"id", packer}}).as_array().size(), 1u );
		Mutate( "restoreOeeMachine( id:$id )", {{"id", m2}} );

		//A machine's purge takes its shape and leaves the floor;  a line's takes its floor with its shapes, and its shape on the
		//floor above;  a floor's takes its shapes.
		CreateShape( bottlingFloor, "Label", jarray{jarray{1, 1}}, {{"label", "Filler"}} );
		Mutate( "purgeOeeMachine( id:$id )", {{"id", m1}} );
		pump1.Id = 0;
		EXPECT_EQ( Count(OeeTable::FloorShapes, "oee_machine_id", m1), 0u );
		EXPECT_EQ( Shapes(floorId), 6u );
		EXPECT_EQ( Floors(plant), 1u );
		Mutate( "purgeOeeLine( id:$id )", {{"id", bottling}} );
		std::erase( lines.Ids, bottling );
		EXPECT_EQ( Floors(bottling), 0u );
		EXPECT_EQ( Shapes(bottlingFloor), 0u );
		EXPECT_EQ( Count(OeeTable::FloorShapes, "member_line_id", bottling), 0u );
		EXPECT_EQ( Shapes(floorId), 5u ) << "the zone, the wall, the aisle, the dock and the packer";
		Mutate( "purgeOeeFloor( id:$id )", {{"id", floorId}} );
		EXPECT_EQ( Shapes(floorId), 0u );
		EXPECT_EQ( Floors(plant), 0u );
		EXPECT_EQ( Status([&]{ Mutate( "purgeOeeFloor( id:$id )", {{"id", floorId}} ); }), EHttpStatus::NotFound );
		EXPECT_TRUE( Mutate("oeeFloorShapes( oeeFloorId:$floor ){ id }", {{"floor", floorId}}).as_array().empty() );
	}
}