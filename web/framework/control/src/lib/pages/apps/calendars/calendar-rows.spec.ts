import { CalendarRows } from './calendar-rows';

//two reads out at once, each answered by the test
const twoReads = ()=>{
	const rows = new CalendarRows<number>();
	const held = ()=>{
		let answer!:( rows:number[] )=>void, fail!:( e:Error )=>void;
		const promise = new Promise<number[]>( ( resolve, reject )=>{ answer = resolve; fail = reject; } );
		return { answer, fail, done: rows.read( ()=>promise, x=>rows.rows.set( x ), ()=>true ) };
	};
	return { rows, first: held(), second: held() };
};

describe( 'CalendarRows', ()=>{
	it( 'keeps the last read asked for, whichever answers last', async ()=>{
		const { rows, first, second } = twoReads();
		second.answer( [2] );
		await second.done;
		expect( rows.rows() ).toEqual( [2] );
		expect( rows.loading() ).toBe( false );
		first.answer( [1] );
		await first.done;
		expect( rows.rows() ).toEqual( [2] );
	} );
	it( 'is loading until the last read answers, and says nothing of one overtaken', async ()=>{
		const { rows, first, second } = twoReads();
		first.fail( new Error( 'overtaken' ) );
		await first.done;
		expect( rows.loading() ).toBe( true );
		expect( rows.error() ).toBeUndefined();
		second.answer( [2] );
		await second.done;
		expect( rows.loading() ).toBe( false );
		expect( rows.rows() ).toEqual( [2] );
	} );
} );