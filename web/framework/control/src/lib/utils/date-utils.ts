export class DateUtils{
	static beginningOfDay( value:Date|null=null ){
		let copy = value ? new Date( value ) : new Date();
		copy.setUTCHours( 0, 0, 0, 0 );
		return copy;
	}
	static asUtc( date:Date ):Date{
		const y = new Date( date.getTime() + date.getTimezoneOffset()*60000 );
		return y;
	}
}