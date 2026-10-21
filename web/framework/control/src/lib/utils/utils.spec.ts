import { fromIsoDuration } from './utils';

const day = 86400;
const year = 365.2425*day;//std::chrono::years - what Chrono::ToString divides by
const thrown = ( str:string ):string|undefined=>{
	try{ fromIsoDuration( str ); }
	catch( e ){ return e instanceof Error ? e.message : "not an Error"; }
	return undefined;
};

//The client's half of restSessionTimeout:  libs/web/server/SettingQL.cpp sends Chrono::ToString( Sessions::RestSessionTimeout() ),
//and ProtoService.loginWait reads it back with this - a throw here fails every login.
describe( 'fromIsoDuration', ()=>{
	it( 'reads an empty string, and a bare P - Chrono::ToString of zero - as no time', ()=>{
		expect( fromIsoDuration("") ).toBe( 0 );
		expect( fromIsoDuration("P") ).toBe( 0 );
		expect( fromIsoDuration("PT") ).toBe( 0 );
	} );

	it( 'reads each time designator', ()=>{
		expect( fromIsoDuration("PT45S") ).toBe( 45 );
		expect( fromIsoDuration("PT30M") ).toBe( 1800 );
		expect( fromIsoDuration("PT2H") ).toBe( 7200 );
		expect( fromIsoDuration("PT1H30M15S") ).toBe( 5415 );
	} );

	it( 'reads each date designator', ()=>{
		expect( fromIsoDuration("P3D") ).toBe( 3*day );
		expect( fromIsoDuration("P2W") ).toBe( 14*day );
		expect( fromIsoDuration("P1Y") ).toBe( 31556952 );
		expect( fromIsoDuration("P1M") ).toBe( 2629746 );
		expect( fromIsoDuration("P12M") ).toBe( fromIsoDuration("P1Y") );
	} );

	it( 'reads M as months before the T and minutes after it', ()=>{
		expect( fromIsoDuration("PT1M") ).toBe( 60 );
		expect( fromIsoDuration("P1MT1M") ).toBe( year/12+60 );
	} );

	it( 'reads a mixed date and time', ()=>{
		expect( fromIsoDuration("P1DT12H") ).toBe( 1.5*day );
		expect( fromIsoDuration("P1Y2M3DT4H5M6S") ).toBe( year+2*year/12+3*day+4*3600+5*60+6 );
		expect( fromIsoDuration("PT36H") ).toBe( fromIsoDuration("P1DT12H") );
	} );

	it( 'reads multi-digit values', ()=>{
		expect( fromIsoDuration("PT120M") ).toBe( 7200 );
		expect( fromIsoDuration("P400D") ).toBe( 400*day );
	} );

	it( 'reads the decimal seconds Chrono::ToString writes for a sub-minute remainder', ()=>{
		expect( fromIsoDuration("PT1.5S") ).toBe( 1.5 );
		expect( fromIsoDuration("PT30M0.25S") ).toBe( 1800.25 );
		expect( fromIsoDuration("PT0.001S") ).toBeCloseTo( 0.001, 9 );
	} );

	it( 'reads the sign Chrono::ToString writes on each component of a negative duration', ()=>{
		expect( fromIsoDuration("PT-30M") ).toBe( -1800 );
		expect( fromIsoDuration("PT-1H-30M") ).toBe( -5400 );//minus 90 minutes:  `d %= hours{1}` keeps the remainder's sign
		expect( fromIsoDuration("P-1DT-1.5S") ).toBe( -86401.5 );
		expect( fromIsoDuration("PT+5S") ).toBe( 5 );
	} );

	it( 'throws on a string without the leading P', ()=>{
		expect( thrown("T30S") ).toContain( "Expected 'P'" );
		expect( thrown("30S") ).toContain( "Expected 'P'" );
		expect( thrown("p1D") ).toContain( "Expected 'P'" );
	} );

	it( 'throws on a designator on the wrong side of the T, an unknown one, or none', ()=>{
		expect( thrown("P1H") ).toContain( "Unknown type 'H'" );
		expect( thrown("P1S") ).toContain( "Unknown type 'S'" );
		expect( thrown("PT1D") ).toContain( "Unknown type 'D'" );
		expect( thrown("PT1W") ).toContain( "Unknown type 'W'" );
		expect( thrown("PT1Y") ).toContain( "Unknown type 'Y'" );
		expect( thrown("P1X") ).toContain( "Unknown type 'X'" );
		expect( thrown("PT30") ).toContain( "Unknown type 'undefined'" );
	} );

	it( 'throws on a malformed number', ()=>{
		expect( thrown("PT1.2.3S") ).toContain( "Bad number '1.2.3'" );
		expect( thrown("PT.S") ).toContain( "Bad number '.'" );
		expect( thrown("PT-.S") ).toContain( "Bad number '-.'" );
	} );

	it( 'throws on a designator with no number, as ToDuration does - not a zero', ()=>{
		expect( thrown("PTS") ).toContain( "Missing number before 'S'" );
		expect( thrown("PT1HM") ).toContain( "Missing number before 'M'" );
		expect( thrown("P-D") ).toContain( "Missing number before 'D'" );
	} );

	it( 'throws on a second T', ()=>{
		expect( thrown("PT1HT1M") ).toContain( "Missing number before 'T'" );
	} );
} );