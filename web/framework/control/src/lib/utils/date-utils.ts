export class DateUtils{
	//A Date as <input type="datetime-local" step="0.001"> shows it, local time to the millisecond, and back.  Not toISOString,
	//which is UTC and ends in Z, which the input refuses.
	static toLocalInput( d:Date ):string{
		const p = ( n:number, w=2 )=>String( n ).padStart( w, '0' );
		return `${p(d.getFullYear(), 4)}-${p(d.getMonth()+1)}-${p(d.getDate())}T${p(d.getHours())}:${p(d.getMinutes())}:${p(d.getSeconds())}.${p(d.getMilliseconds(), 3)}`;
	}
	//In the hour DST repeats the text names two times, and `new Date` takes the earlier.  `from`, the time the text was edited
	//from, moved by the edit is the other where its text is still the one typed:  changing the milliseconds of a time in the
	//later hour keeps it there.
	static fromLocalInput( s:string, from?:Date ):Date|undefined{
		if( !s )
			return undefined;
		const d = new Date( s );//no zone in the text, so local time, as the input means it
		if( isNaN( d.getTime() ) )
			return undefined;
		if( from ){
			const wall = ( text:string )=>Date.parse( text+"Z" );//the text's clock reading, as one with no DST
			const moved = new Date( from.getTime() + wall( DateUtils.toLocalInput(d) ) - wall( DateUtils.toLocalInput(from) ) );
			if( DateUtils.toLocalInput( moved )==DateUtils.toLocalInput( d ) )
				return moved;
		}
		return d;
	}
}