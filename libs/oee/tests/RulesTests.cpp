//Every refusal of the rules (docs/oee/spec.md *Machines*, *Intervals*, *Lines*), each a BadRequest whose message the
//client sees, with no server.
#include "helpers.h"

namespace Jde::Opc::Oee::Tests{
	Ξ Has( str message, sv part )ι->::testing::AssertionResult{
		return message.find( part )!=string::npos ? ::testing::AssertionSuccess() : ::testing::AssertionFailure() << "'" << message << "' lacks '" << part << "'";
	}

	TEST( Rules, Width ){
		for( let m : {1, 2, 3, 4, 5, 6, 10, 12, 15, 20, 30, 60} )
			EXPECT_EQ( Refused([m]{ ValidateWidth(minutes{m}); }), "" ) << m;
		for( let width : {Duration{0s}, Duration{90s}, Duration{7min}, Duration{45min}, Duration{2h}, Duration{-1min}} )
			EXPECT_TRUE( Has(Refused([width]{ ValidateWidth(width); }), "widths that divide an hour") ) << Chrono::ToString( width );
	}

	TEST( Rules, SignalSet ){
		auto refused = []( std::initializer_list<std::pair<ERole,uint32>> roles ){ return Refused( [&]{ ValidateSignals(Signals(roles)); } ); };
		EXPECT_EQ( refused({{ERole::State, 1}}), "" );
		EXPECT_EQ( refused({{ERole::Total, 2}}), "" );
		EXPECT_EQ( refused({{ERole::Rate, 6}}), "" );
		EXPECT_EQ( refused({{ERole::State, 1}, {ERole::Total, 2}, {ERole::Good, 3}, {ERole::Reject, 4}, {ERole::Reason, 5}}), "" );
		EXPECT_EQ( refused({{ERole::State, 1}, {ERole::Good, 3}, {ERole::Reject, 4}}), "" ) << "a State, with the total the two summed";

		EXPECT_TRUE( Has(refused({}), "neither a State signal nor a count") );
		EXPECT_TRUE( Has(refused({{ERole::Good, 3}, {ERole::Reject, 4}}), "neither a State signal nor a count") );
		EXPECT_TRUE( Has(refused({{ERole::Reason, 5}}), "neither a State signal nor a count") );
		EXPECT_TRUE( Has(refused({{ERole::Total, 2}, {ERole::Rate, 6}}), "a count is one or the other") );
		EXPECT_TRUE( Has(refused({{ERole::State, 1}, {ERole::Total, 2}, {ERole::Rate, 6}}), "a count is one or the other") );
		EXPECT_TRUE( Has(refused({{ERole::State, 1}, {ERole::State, 7}}), "two State signals") );
		EXPECT_TRUE( Has(refused({{ERole::Total, 2}, {ERole::Good, 3}, {ERole::Good, 8}}), "two Good signals") );

		auto none = Signals( {{ERole::State, 1}} );
		none[0].Running.clear();
		EXPECT_TRUE( Has(Refused([&]{ ValidateSignals(none); }), "would never run") );
		auto rate = Signals( {{ERole::Rate, 6}} );
		rate[0].RatePeriod = Duration::zero();
		EXPECT_TRUE( Has(Refused([&]{ ValidateSignals(rate); }), "ratePeriod must be above zero") );
	}

	TEST( Rules, Machine ){
		let stated = Signals( {{ERole::State, 1}, {ERole::Total, 2}} );
		let counted = Signals( {{ERole::Total, 2}} );
		EXPECT_EQ( Refused([&]{ ValidateMachine(stated, 1min, nullopt, nullopt); }), "" );
		EXPECT_EQ( Refused([&]{ ValidateMachine(stated, 15min, 1s, 5min); }), "" );
		EXPECT_EQ( Refused([&]{ ValidateMachine(counted, 1min, 1s, 30s); }), "" );
		EXPECT_TRUE( Has(Refused([&]{ ValidateMachine(counted, 1min, 1s, nullopt); }), "needs an idleTimeout") );
		EXPECT_TRUE( Has(Refused([&]{ ValidateMachine(Signals({{ERole::Rate, 6}}), 1min, nullopt, nullopt); }), "needs an idleTimeout") );
		EXPECT_TRUE( Has(Refused([&]{ ValidateMachine(stated, 1min, Duration::zero(), nullopt); }), "idealCycle must be above zero") );
		EXPECT_TRUE( Has(Refused([&]{ ValidateMachine(stated, 1min, nullopt, Duration::zero()); }), "idleTimeout must be above zero") );
		EXPECT_TRUE( Has(Refused([&]{ ValidateMachine(stated, 7min, nullopt, nullopt); }), "widths that divide an hour") );
		EXPECT_TRUE( Has(Refused([&]{ ValidateMachine(Signals({{ERole::Good, 3}}), 1min, nullopt, 30s); }), "neither a State signal nor a count") );
	}

	TEST( Rules, Running ){
		EXPECT_EQ( ParseRunning(nullptr), vector<StateValue>{StateValue{1.0}} ) << "[true] by default";
		EXPECT_EQ( ParseRunning(""), vector<StateValue>{StateValue{1.0}} );
		EXPECT_EQ( ParseRunning("[true]"), vector<StateValue>{StateValue{1.0}} );
		EXPECT_EQ( ParseRunning("[2,3]"), (vector<StateValue>{StateValue{2.0}, StateValue{3.0}}) );
		EXPECT_EQ( ParseRunning(jarray{false, 4, "RUN"}), (vector<StateValue>{StateValue{0.0}, StateValue{4.0}, StateValue{string{"RUN"}}}) );
		EXPECT_TRUE( Has(Refused([]{ ParseRunning("[]"); }), "would never run") );
		EXPECT_TRUE( Has(Refused([]{ ParseRunning("{}"); }), "running must be a json list") );
		EXPECT_TRUE( Has(Refused([]{ ParseRunning("[null]"); }), "running must be a json list") );
		EXPECT_TRUE( Has(Refused([]{ ParseRunning("[2,"); }), "running must be a json list") );
		EXPECT_TRUE( Has(Refused([]{ ParseRunning(3); }), "running must be a json list") );
	}

	using Lines::Graph;
	//A site of an area, the area of two lines and a machine, a line of a cell and a machine.
	Ξ Plant()ι->Graph{
		return {
			{{1, "Site", ELevel::Site}, {2, "Area", ELevel::Area}, {3, "Bottling", ELevel::Line}, {4, "Canning", ELevel::Line}, {5, "Cell", ELevel::Cell}},
			{{1, nullopt, 2}, {2, nullopt, 3}, {2, nullopt, 4}, {2, 10}, {3, nullopt, 5}, {3, 11}, {5, 12}, {5, 13}, {4, 14}},
			{{10, "Mixer"}, {11, "Filler"}, {12, "Capper"}, {13, "Labeller"}, {14, "Seamer"}}
		};
	}
	Ξ With( Graph graph, Lines::Member member )ι->Graph{
		graph.Members.push_back( member );
		return graph;
	}
	Ξ LinesRefused( const Graph& graph )->string{ return Refused( [&]{ Lines::Validate(graph); } ); }

	TEST( Rules, LineReach ){
		let plant = Plant();
		EXPECT_EQ( LinesRefused(plant), "" );
		EXPECT_EQ( Lines::Reach(plant, 1), (vector<uint32>{12, 13, 11, 14, 10}) ) << "depth first, in member order";
		EXPECT_EQ( Lines::Reach(plant, 3), (vector<uint32>{12, 13, 11}) );
		EXPECT_EQ( Lines::Reach(plant, 5), (vector<uint32>{12, 13}) );
		EXPECT_TRUE( Has(Refused([&]{ Lines::Reach(plant, 9); }), "Line 9 is not one of the gateway's") );

		//A line in several lines, and a machine in several, is no refusal:  a reporting line across the areas.
		auto reporting = plant;
		reporting.Lines.push_back( {6, "Fillers", ELevel::Line} );
		reporting.Members.push_back( {6, 11} );
		reporting.Members.push_back( {6, nullopt, 5} );
		EXPECT_EQ( LinesRefused(reporting), "" );
		EXPECT_EQ( Lines::Reach(reporting, 6), (vector<uint32>{11, 12, 13}) );
	}
	TEST( Rules, LineRefusals ){
		let plant = Plant();
		EXPECT_TRUE( Has(LinesRefused(With(plant, {3, nullopt, 3})), "Line 'Bottling' would reach itself through 'Bottling'") );
		EXPECT_TRUE( Has(LinesRefused({{{1, "A", ELevel::Line}, {2, "B", ELevel::Line}, {3, "C", ELevel::Line}}, {{1, nullopt, 2}, {2, nullopt, 3}, {3, nullopt, 1}}, {}}), "Line 'A' would reach itself through 'C'") );
		EXPECT_TRUE( Has(LinesRefused(With(plant, {3, 11})), "'Bottling' would reach machine 'Filler' by two paths") );
		EXPECT_TRUE( Has(LinesRefused(With(plant, {3, 12})), "'Bottling' would reach machine 'Capper' by two paths") ) << "its cell holds it";
		EXPECT_TRUE( Has(LinesRefused(With(plant, {5, 10})), "'Area' would reach machine 'Mixer' by two paths") ) << "the area holds it, and now the cell under it does";
		EXPECT_TRUE( Has(LinesRefused(With(plant, {4, nullopt, 5})), "'Area' would reach machine 'Capper' by two paths") ) << "it reaches the cell through both lines";
		EXPECT_TRUE( Has(LinesRefused(With(With(plant, {4, 99}), {2, 99})), "'Area' would reach machine '99' by two paths") );
		EXPECT_EQ( LinesRefused(With(plant, {4, 99})), "" );
		EXPECT_TRUE( Has(LinesRefused(With(plant, {3, 12, 5})), "one machine or one line, not both and not neither") );
		EXPECT_TRUE( Has(LinesRefused(With(plant, {3})), "one machine or one line, not both and not neither") );
		EXPECT_TRUE( Has(LinesRefused(With(plant, {9, 10})), "Line 9 is not one of the gateway's") );
		EXPECT_TRUE( Has(LinesRefused(With(plant, {3, nullopt, 9})), "Line 9, a member of 'Bottling', is not one of the gateway's") );
	}
	//A member's level is no higher than its line's:  an area holds areas and lines, a line lines and cells.
	TEST( Rules, LineLevels ){
		auto levels = []( ELevel parent, ELevel member ){
			return LinesRefused( {{{1, "Parent", parent}, {2, "Member", member}}, {{1, nullopt, 2}}, {}} );
		};
		EXPECT_EQ( levels(ELevel::Site, ELevel::Area), "" );
		EXPECT_EQ( levels(ELevel::Area, ELevel::Area), "" );
		EXPECT_EQ( levels(ELevel::Area, ELevel::Line), "" );
		EXPECT_EQ( levels(ELevel::Line, ELevel::Line), "" );
		EXPECT_EQ( levels(ELevel::Line, ELevel::Cell), "" );
		EXPECT_EQ( levels(ELevel::Site, ELevel::Cell), "" );
		EXPECT_TRUE( Has(levels(ELevel::Area, ELevel::Site), "'Member' is a Site, above 'Parent', an Area") );
		EXPECT_TRUE( Has(levels(ELevel::Line, ELevel::Area), "'Member' is an Area, above 'Parent', a Line") );
		EXPECT_TRUE( Has(levels(ELevel::Cell, ELevel::Line), "'Member' is a Line, above 'Parent', a Cell") );
		//A level changed under its members, or over its line.
		auto plant = Plant();
		plant.Lines[2].Level = ELevel::Cell;
		EXPECT_EQ( LinesRefused(plant), "" ) << "a cell of a cell";
		plant.Lines[4].Level = ELevel::Line;
		EXPECT_TRUE( Has(LinesRefused(plant), "'Cell' is a Line, above 'Bottling', a Cell") );
	}
}