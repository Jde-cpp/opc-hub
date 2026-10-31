export class DateUtils{
	//A Date as <input type="datetime-local" step="0.001"> shows it, local time to the millisecond, and back.  Not toISOString,
	//which is UTC and ends in Z, which the input refuses.
	static toLocalInput( d:Date ):string{
		const p = ( n:number, w=2 )=>String( n ).padStart( w, '0' );
		return `${p(d.getFullYear(), 4)}-${p(d.getMonth()+1)}-${p(d.getDate())}T${p(d.getHours())}:${p(d.getMinutes())}:${p(d.getSeconds())}.${p(d.getMilliseconds(), 3)}`;
	}
	static fromLocalInput( s:string ):Date|undefined{
		if( !s )
			return undefined;
		const d = new Date( s );//no zone in the text, so local time, as the input means it
		return isNaN( d.getTime() ) ? undefined : d;
	}
}