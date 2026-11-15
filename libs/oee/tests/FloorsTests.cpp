//Demo phase 3 (#480):  the floor's rules (docs/demo/spec.md *The floor*), each refusal a BadRequest whose message the client
//sees, with no server:  a plant over Bottling, with the filler and the capper, and Packing, with the packer and a dock cell
//under it with the palletizer;  a reporting line over the filler alone.
#include "helpers.h"

namespace Jde::Opc::Oee::Tests{
	using namespace Floors;
	using Lines::Graph;
	constexpr uint32 Plant{ 1 }, Bottling{ 2 }, Packing{ 3 }, Dock{ 4 }, Reporting{ 5 };
	constexpr uint32 Filler{ 11 }, Capper{ 12 }, Packer{ 13 }, Palletizer{ 14 };

	struct FloorsTests : ::testing::Test{
		const Graph Site{
			.Lines={ {Plant, "Plant", ELevel::Site}, {Bottling, "Bottling", ELevel::Line}, {Packing, "Packing", ELevel::Line}, {Dock, "Dock", ELevel::Cell}, {Reporting, "Reporting", ELevel::Line} },
			.Members={ {Plant, nullopt, Bottling}, {Plant, nullopt, Packing}, {Bottling, Filler, nullopt}, {Bottling, Capper, nullopt}, {Packing, Packer, nullopt}, {Packing, nullopt, Dock}, {Dock, Palletizer, nullopt}, {Reporting, Filler, nullopt} },
			.Machines={ {Filler, "Filler"}, {Capper, "Capper"}, {Packer, "Packer"}, {Palletizer, "Palletizer"} }
		};
		const Floor Hall{ 1, Plant, 40, 20, 0.5 };//the spec's.
		const Floor BottlingHall{ 2, Bottling, 20, 10, 0.5 };
		const vector<Point> Box{ {3, 3}, {9, 3}, {9, 7}, {3, 7} };
		Ω Shape( EKind kind, vector<Point> points, optional<uint32> machine={}, optional<uint32> line={}, uint32 id=0 )ι->Floors::Shape{ return { id, 1, kind, machine, line, move(points) }; }
		//What the shape's refusal says on `floor`, or empty when it passes.
		α Refuse( const Floors::Shape& shape, const vector<Floors::Shape>& others={}, const Floor* floor=nullptr )->string{
			return Refused( [&]{ ValidateShape( Site, floor ? *floor : Hall, shape, others ); } );
		}
	};

	//The lines a line reaches, each once, itself left out, beside the machines.
	TEST_F( FloorsTests, Reach ){
		EXPECT_EQ( Lines::ReachLines(Site, Plant), (vector<uint32>{Bottling, Packing, Dock}) );
		EXPECT_EQ( Lines::Reach(Site, Plant), (vector<uint32>{Filler, Capper, Packer, Palletizer}) );
		EXPECT_EQ( Lines::ReachLines(Site, Packing), vector<uint32>{Dock} );
		EXPECT_TRUE( Lines::ReachLines(Site, Bottling).empty() );
		EXPECT_THROW( Lines::ReachLines(Site, 99), Exception );
		Graph twice{ .Lines={ {1, "a", ELevel::Area}, {2, "b", ELevel::Line}, {3, "c", ELevel::Line}, {4, "d", ELevel::Cell} }, .Members={ {1, nullopt, 2}, {1, nullopt, 3}, {2, nullopt, 4}, {3, nullopt, 4} } };
		EXPECT_EQ( Lines::ReachLines(twice, 1), (vector<uint32>{2, 4, 3}) ) << "a line reached by two paths, once, depth first";
	}

	//A kind's text, as the column holds it, and the points' json.
	TEST_F( FloorsTests, KindsAndPoints ){
		for( let kind : {EKind::Machine, EKind::Line, EKind::Zone, EKind::Wall, EKind::Aisle, EKind::Label} )
			EXPECT_EQ( ToKind(ToString(kind)), optional<EKind>{kind} );
		EXPECT_FALSE( ToKind("Box") );
		EXPECT_FALSE( ToKind("machine") );
		EXPECT_EQ( ParsePoints("[[3,3],[9,3],[9,7],[3,7]]"), Box ) << "the column's text";
		EXPECT_EQ( ParsePoints(jarray{jarray{3, 3}, jarray{9, 3}, jarray{9, 7}, jarray{3, 7}}), Box ) << "a caller's list";
		EXPECT_EQ( ParsePoints("[[0.5,1.25]]"), (vector<Point>{{0.5, 1.25}}) );
		EXPECT_EQ( ToJson(Box), "[[3,3],[9,3],[9,7],[3,7]]" ) << "whole metres spelled whole";
		EXPECT_EQ( ToJson(vector<Point>{{0.5, 1.25}, {38, 15}}), "[[0.5,1.25],[38,15]]" );
		for( let text : {"[]", "[[1]]", "[[1,2,3]]", "[[1,\"2\"]]", "[1,2]", "{}", "none", "[[1,2],3]"} )
			EXPECT_NE( Refused([text]{ ParsePoints(text); }).find("points"), string::npos ) << text;
		EXPECT_NE( Refused([]{ ParsePoints(nullptr); }).find("points"), string::npos ) << "null, a create without them";
	}

	TEST_F( FloorsTests, Floor ){
		EXPECT_EQ( Refused([&]{ ValidateFloor(Hall); }), "" );
		EXPECT_EQ( Refused([]{ ValidateFloor({0, 1, 0.25, 0.25, 0.25}); }), "" );
		EXPECT_NE( Refused([]{ ValidateFloor({0, 1, 0, 20, 0.5}); }).find("width must be above zero"), string::npos );
		EXPECT_NE( Refused([]{ ValidateFloor({0, 1, 40, -1, 0.5}); }).find("height must be above zero"), string::npos );
		EXPECT_NE( Refused([]{ ValidateFloor({0, 1, 40, 20, 0}); }).find("grid must be above zero"), string::npos );
		EXPECT_NE( Refused([]{ ValidateFloor({0, 1, std::numeric_limits<double>::infinity(), 20, 0.5}); }).find("width"), string::npos );
		//A resize keeps every shape inside.
		const vector<Floors::Shape> shapes{ Shape(EKind::Machine, Box, Filler), Shape(EKind::Label, {{38, 15}}) };
		EXPECT_EQ( Refused([&]{ ValidateInside(Hall, shapes); }), "" );
		EXPECT_EQ( Refused([&]{ ValidateInside({1, Plant, 38, 15, 0.5}, shapes); }), "" ) << "on the edge";
		EXPECT_NE( Refused([&]{ ValidateInside({1, Plant, 30, 20, 0.5}, shapes); }).find("[38, 15] is outside the floor, 30 by 20"), string::npos );
		EXPECT_NE( Refused([&]{ ValidateInside({1, Plant, 40, 6, 0.5}, shapes); }).find("outside"), string::npos );
	}

	//A shape of each kind passes, and each refusal:  the kind, the key its kind takes and no other, the reach, once on a floor,
	//the point counts and every point inside.
	TEST_F( FloorsTests, Shapes ){
		EXPECT_EQ( Refuse(Shape(EKind::Machine, Box, Filler)), "" );
		EXPECT_EQ( Refuse(Shape(EKind::Machine, Box, Palletizer)), "" ) << "reached through Packing and the Dock";
		EXPECT_EQ( Refuse(Shape(EKind::Line, {{1, 1}, {20, 1}, {20, 9}, {1, 9}}, nullopt, Bottling)), "" );
		EXPECT_EQ( Refuse(Shape(EKind::Line, {{1, 1}, {20, 1}, {20, 9}}, nullopt, Dock)), "" ) << "a line reached through a line";
		EXPECT_EQ( Refuse(Shape(EKind::Zone, {{9, 4.5}, {13, 4.5}, {13, 5.5}, {9, 5.5}})), "" );
		EXPECT_EQ( Refuse(Shape(EKind::Wall, {{0, 0}, {40, 0}, {40, 20}, {0, 20}, {0, 0}})), "" ) << "on the edge, closed by hand";
		EXPECT_EQ( Refuse(Shape(EKind::Aisle, {{0, 10}, {40, 10}})), "" );
		EXPECT_EQ( Refuse(Shape(EKind::Label, {{38, 15}})), "" );

		Floors::Shape unknown = Shape( EKind::Label, {{1, 1}} );
		unknown.Kind = nullopt;
		EXPECT_NE( Refuse(unknown).find("Machine, Line, Zone, Wall, Aisle or Label"), string::npos );
		EXPECT_NE( Refuse(Shape(EKind::Machine, Box)).find("oeeMachineId is required"), string::npos );
		EXPECT_NE( Refuse(Shape(EKind::Machine, Box, Filler, Bottling)).find("not a line"), string::npos );
		EXPECT_NE( Refuse(Shape(EKind::Machine, Box, Packer), {}, &BottlingHall).find("'Packer' isn't one line 'Bottling' reaches"), string::npos );
		EXPECT_NE( Refuse(Shape(EKind::Machine, Box, 99)).find("'99' isn't one line 'Plant' reaches"), string::npos ) << "named by its number";
		const vector<Floors::Shape> drawn{ Shape(EKind::Machine, Box, Filler, nullopt, 7), Shape(EKind::Line, Box, nullopt, Bottling, 8) };
		EXPECT_NE( Refuse(Shape(EKind::Machine, Box, Filler), drawn).find("'Filler' is on the floor already"), string::npos );
		EXPECT_EQ( Refuse(Shape(EKind::Machine, Box, Filler, nullopt, 7), drawn), "" ) << "its own row, an update";
		EXPECT_EQ( Refuse(Shape(EKind::Machine, Box, Capper), drawn), "" );
		EXPECT_NE( Refuse(Shape(EKind::Line, Box)).find("memberLineId is required"), string::npos );
		EXPECT_NE( Refuse(Shape(EKind::Line, Box, Filler, Bottling)).find("not a machine"), string::npos );
		EXPECT_NE( Refuse(Shape(EKind::Line, Box, nullopt, Plant)).find("'Plant' is the floor's own"), string::npos );
		EXPECT_NE( Refuse(Shape(EKind::Line, Box, nullopt, Reporting)).find("'Reporting' isn't one line 'Plant' reaches"), string::npos );
		EXPECT_NE( Refuse(Shape(EKind::Line, Box, nullopt, Packing), {}, &BottlingHall).find("'Packing' isn't one line 'Bottling' reaches"), string::npos );
		EXPECT_NE( Refuse(Shape(EKind::Line, Box, nullopt, Bottling), drawn).find("'Bottling' is on the floor already"), string::npos );
		EXPECT_NE( Refuse(Shape(EKind::Zone, Box, Filler)).find("A Zone shape names no machine or line"), string::npos );
		EXPECT_NE( Refuse(Shape(EKind::Label, {{1, 1}}, nullopt, Bottling)).find("A Label shape names no machine or line"), string::npos );

		EXPECT_NE( Refuse(Shape(EKind::Machine, {{3, 3}, {9, 7}}, Filler)).find("A Machine shape is a closed polygon of three points or more, not 2"), string::npos );
		EXPECT_NE( Refuse(Shape(EKind::Line, {{3, 3}, {9, 7}}, nullopt, Bottling)).find("three points or more, not 2"), string::npos );
		EXPECT_NE( Refuse(Shape(EKind::Zone, {{3, 3}})).find("three points or more, not 1"), string::npos );
		EXPECT_NE( Refuse(Shape(EKind::Wall, {{3, 3}})).find("A Wall is a polyline of two points or more, not 1"), string::npos );
		EXPECT_NE( Refuse(Shape(EKind::Aisle, {{3, 3}})).find("two points or more, not 1"), string::npos );
		EXPECT_NE( Refuse(Shape(EKind::Label, {{3, 3}, {9, 7}})).find("A Label is one point, not 2"), string::npos );
		EXPECT_NE( Refuse(Shape(EKind::Machine, {{39, 19}, {41, 19}, {41, 21}}, Filler)).find("[41, 19] is outside the floor, 40 by 20"), string::npos );
		EXPECT_NE( Refuse(Shape(EKind::Label, {{-0.5, 1}})).find("outside"), string::npos );
		EXPECT_NE( Refuse(Shape(EKind::Aisle, {{0, 10}, {40.5, 10}})).find("outside"), string::npos );
		EXPECT_EQ( Refuse(Shape(EKind::Label, {{40, 20}})), "" ) << "the far corner is on the floor";
	}
}