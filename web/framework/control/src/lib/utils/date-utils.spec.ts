import { DateUtils } from './date-utils';

//the text a datetime-local input holds:  the Children tab's DateTime editor and the history edit dialog's times
describe( 'DateUtils local time inputs', ()=>{
	it( 'round-trips a Date to the millisecond in local time', ()=>{
		const d = new Date( 2026, 9, 9, 13, 4, 5, 6 );
		expect( DateUtils.toLocalInput( d ) ).toBe( '2026-10-09T13:04:05.006' );
		expect( DateUtils.fromLocalInput( DateUtils.toLocalInput( d ) )?.getTime() ).toBe( d.getTime() );
		expect( DateUtils.fromLocalInput( '' ) ).toBeUndefined();
		expect( DateUtils.fromLocalInput( 'not a time' ) ).toBeUndefined();
	} );
} );