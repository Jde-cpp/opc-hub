#include "qlTestsProcs.h"

//Twin of the *generated* qlt_item_insert proc - column order/defaults match TableDdl::InsertProcCreateStatement for qlt_items:
//insertable columns in `i` order, created=$now, the sequence column out last.
//	params: [0]=_item_type_id, [1]=_name, [2]=_slug, [3]=_attributes, [4]=_description; out _item_id returned as the result row.
namespace Jde::DB::Sqlite::QLTestsProcs{
	α RegisterQltItemInsert( IProcs& procs )ι->void{
		RegisterInsertProc( procs, "qlt_item_insert", "insert into qlt_items( item_type_id, name, slug, attributes, created, description ) values( ?, ?, ?, ?, unixepoch(), ? )", 5 );
	}
}