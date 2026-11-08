import Long from "long";

export type Timestamp = {seconds:number|Long; nanos:number;}
export type Duration = {seconds:number|Long; nanos:number;}
export class ProtoUtils{
	static toNumber( value:number|Long ):number{
		return typeof(value)==='object' ? value.toNumber() : value;
	}

	static toDate( value:Timestamp ):Date|null{
		return !value || value.seconds==0 ? null : new Date( ProtoUtils.toNumber(value.seconds)*1000 + value.nanos/1000000 );
	}
	//nanos from the floored seconds, so they stay in [0, 1e9) before 1970:  `ms % 1000` is negative there, and the fraction counted
	//twice - 23:59:58.500 on 1969-12-31 came out {-2 s, -0.5 s}, a second early (reviews-issue/423-clients-writes.md #8)
	static fromDate( value:Date ):Timestamp|null{
		if( !value )
			return null;
		const ms = value.getTime(), seconds = Math.floor( ms/1000 );
		return { seconds: Long.fromNumber( seconds ), nanos: (ms - seconds*1000) * 1000000 };
	}
}