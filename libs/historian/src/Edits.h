#pragma once
#include <absl/functional/any_invocable.h>
#include <jde/historian/Group.h>
#include "io/DayFiles.h"

namespace Jde::Opc::Hist{
	//An edit as the flush runs it (Edit.cpp):  its entries and their results as they stand, the days it reaches, and what
	//it changed.
	struct Group::Editing final{
		Editing( vector<EditDetails> details, Writer by, EditAwait* waiter, SL sl )ι:Details{ move(details) }, By{ move(by) }, Waiter{ waiter }, Sl{ sl }{ Results.resize( Details.size() ); }
		//The day couldn't be made or written:  each of its values, and each entry's status that depends on it, is
		//Bad_UnexpectedError.
		α Fail( Day day )ι->void{
			if( !Failed.insert(day).second )
				return;
			for( const auto& [entry,value] : ByDay[day] )
				( value ? Results[entry].Results[*value] : Results[entry].Status ) = UA_STATUSCODE_BADUNEXPECTEDERROR;
		}
		vector<EditDetails> Details;
		Writer By;
		EditAwait* Waiter;
		SL Sl;
		vector<EditResult> Results;
		//Each day's entries and values, (entry, value), or (entry, none) for an entry's own status, which fail with the day.
		std::map<Day,vector<std::pair<uint,optional<uint>>>> ByDay;
		flat_set<Day> Unflushed;//each day the flush before couldn't write all of, whose file lacks what the edit would look at.
		flat_set<Day> Failed;
		vector<std::pair<NodeIndex,Ticks>> Changed;//each record written as changed:  what the newest and the corrections look at.
		bool Abandoned{};//the historian ended before it was written.
	};
	//A write the flush does after its batch:  a correction's, or an edit's.  Done, when set, runs once it is committed,
	//or has failed.
	struct Group::Job final{
		Day Date;
		DayWrite Write;
		absl::AnyInvocable<void( bool )> Done;
	};
}