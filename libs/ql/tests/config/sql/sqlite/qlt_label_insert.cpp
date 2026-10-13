#include "qlTestsProcs.h"

//Twin of the *generated* qlt_label_insert proc - qlt_labels has no created column, so the insert is just the two insertable columns.
//	params: [0]=_name, [1]=_color; out _label_id returned as the result row.
namespace Jde::DB::Sqlite::QLTestsProcs{
	α RegisterQltLabelInsert( IProcs& procs )ι->void{
		RegisterInsertProc( procs, "qlt_label_insert", "insert into qlt_labels( name, color ) values( ?, ? )", 2 );
	}
}