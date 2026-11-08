import { ProtoUtils } from './proto-utils';

//a Date as a google.protobuf.Timestamp:  the write dialogs' DateTime (ValueEditor) and a push's date (Gateway.toValue)
describe( 'ProtoUtils.fromDate', ()=>{
	it( 'keeps the nanos in [0, 1e9) before 1970', ()=>{//reviews-issue/423-clients-writes.md #8:  -1.5 s came out -2.5 s
		const t = ProtoUtils.fromDate( new Date( -1500 ) )!;
		expect( ProtoUtils.toNumber( t.seconds ) ).toBe( -2 );
		expect( t.nanos ).toBe( 500_000_000 );
		expect( ProtoUtils.toDate( t )?.getTime() ).toBe( -1500 );
	} );
	it( 'splits a time after 1970 into its seconds and the rest', ()=>{
		const t = ProtoUtils.fromDate( new Date( 1_800_000_000_250 ) )!;
		expect( ProtoUtils.toNumber( t.seconds ) ).toBe( 1_800_000_000 );
		expect( t.nanos ).toBe( 250_000_000 );
		expect( ProtoUtils.toDate( t )?.getTime() ).toBe( 1_800_000_000_250 );
	} );
	it( 'gives a whole second no nanos, either side of 1970', ()=>{
		expect( ProtoUtils.fromDate( new Date( -3000 ) )!.nanos ).toBe( 0 );
		expect( ProtoUtils.fromDate( new Date( 3000 ) )!.nanos ).toBe( 0 );
	} );
} );