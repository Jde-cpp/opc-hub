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

//reviews-issue/412-clients-web-fixes.md #9:  editing a time in the hour DST repeats moved it to the earlier of the two
describe( 'DateUtils.fromLocalInput in the hour DST repeats', ()=>{
	beforeEach( ()=>vi.stubEnv( 'TZ', 'America/New_York' ) );
	afterEach( ()=>vi.unstubAllEnvs() );
	const later = new Date( Date.UTC(2026, 10, 1, 6, 30) );//01:30 EST, the hour's second time
	it( 'keeps an edit of a time in the later hour there', ()=>{
		expect( DateUtils.toLocalInput( later ) ).toBe( '2026-11-01T01:30:00.000' );
		expect( DateUtils.fromLocalInput( '2026-11-01T01:30:00.500', later )?.getTime() ).toBe( later.getTime() + 500 );
		expect( DateUtils.fromLocalInput( '2026-11-01T01:30:00.500' )?.getTime() ).toBe( later.getTime() + 500 - 3600_000 );//alone, the earlier
	} );
	it( 'takes the text as typed where the edit leaves the hour', ()=>{
		expect( DateUtils.fromLocalInput( '2026-11-01T00:30:00.000', later )?.getTime() ).toBe( Date.UTC(2026, 10, 1, 4, 30) );//00:30 EDT
	} );
} );