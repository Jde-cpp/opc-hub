import { computed } from '@angular/core';
import { OpcError } from './opc-error';
import { EHistorian, ELimit, ESeverity, flagsText, infoBits, isBad, nameKey, scHex, severity, statusIcon, statusText } from './status-code';

//OPC 10000-4 7.38.  The codes are the stack's own:  0x40940000 UncertainEngineeringUnitsExceeded, 0x808C0000 BadSensorFailure,
//0x00960000 GoodLocalOverride.  InfoType (11:10) = 01 is 0x0400 - the bit that gives the limit and overflow bits a meaning.
describe( 'status-code', ()=>{
	it( 'reads the severity off the top two bits', ()=>{
		expect( severity(0) ).toBe( ESeverity.Good );
		expect( severity(undefined) ).toBe( ESeverity.Good );
		expect( severity(0x00960000) ).toBe( ESeverity.Good );
		expect( severity(0x40940000) ).toBe( ESeverity.Uncertain );
		expect( severity(0x808C0000) ).toBe( ESeverity.Bad );
		expect( severity(0xC0000000) ).toBe( ESeverity.Reserved );
		expect( [0, 0x40940000, 0x808C0000, 0xC0000000].map(isBad) ).toEqual( [false, false, true, true] );//the reserved 11 is bad, as UA_StatusCode_isBad has it
	} );

	//`sc & 0xFFFF0000` is an int32:  negative for every Bad code, which the server cannot parse out of ?scs= - so the name
	//never came back and the fetch repeated forever.
	it( 'keys a name by the code less its flags, as an unsigned number', ()=>{
		expect( nameKey(0x808C0600) ).toBe( 0x808C0000 );
		expect( nameKey(0x808C0600) ).toBeGreaterThan( 0 );
		expect( nameKey(0x40940600) ).toBe( 0x40940000 );
		expect( nameKey(0x00000480) ).toBe( 0 );
	} );

	it( 'decodes the limit and overflow bits only for a DataValue InfoType', ()=>{
		expect( infoBits(0x40940500).limit ).toBe( ELimit.Low );
		expect( infoBits(0x40940600).limit ).toBe( ELimit.High );
		expect( infoBits(0x40940700).limit ).toBe( ELimit.Constant );
		expect( infoBits(0x00000480) ).toEqual( {limit: ELimit.None, overflow: true, structureChanged: false, semanticsChanged: false, historian: EHistorian.Raw, partial: false, extraData: false, multiValue: false} );
		expect( infoBits(0x40940200).limit ).toBe( ELimit.None );//the limit bits with no InfoType say nothing
		expect( infoBits(0x40940080).overflow ).toBe( false );
	} );

	it( 'decodes StructureChanged and SemanticsChanged whatever the InfoType, and the historian bits only with one', ()=>{
		expect( infoBits(0x00008000).structureChanged ).toBe( true );
		expect( infoBits(0x00004000).semanticsChanged ).toBe( true );
		expect( flagsText(0x4094001F) ).toBe( "" );//4:0 with no InfoType say nothing
		expect( flagsText(0x4094041F) ).toBe( "+Partial+ExtraData+MultiValue" );//1:0 = 3 is reserved
	} );
	//Part 11's historian bits (Table 20) on a history read's values:  Calculated, every aggregate's, says nothing on screen;
	//Interpolated and Partial qualify the value, and ExtraData and MultiValue say a record hides others
	it( 'decodes the historian bits and words those that qualify a value', ()=>{
		expect( infoBits(0x00000401) ).toMatchObject( {historian: EHistorian.Calculated, partial: false, extraData: false, multiValue: false} );
		expect( infoBits(0x00000402).historian ).toBe( EHistorian.Interpolated );
		expect( infoBits(0x00000405) ).toMatchObject( {historian: EHistorian.Calculated, partial: true} );
		expect( infoBits(0x00000418) ).toMatchObject( {historian: EHistorian.Raw, extraData: true, multiValue: true} );
		expect( infoBits(0x00000001).historian ).toBe( EHistorian.Raw );//no InfoType
		expect( flagsText(0x00000401) ).toBe( "" );
		expect( flagsText(0x00000402) ).toBe( "+Interpolated" );
		expect( flagsText(0x00000405) ).toBe( "+Partial" );
		expect( statusText(0x00000401) ).toBe( "Good" );
		expect( statusText(0x40B70406, "UncertainDataSubNormal") ).toBe( "UncertainDataSubNormal+Interpolated+Partial" );
	} );

	it( 'words the flags in the emulator\'s order', ()=>{
		expect( flagsText(0x4094C680) ).toBe( "+High+Overflow+StructureChanged+SemanticsChanged" );
		expect( flagsText(0) ).toBe( "" );
		expect( flagsText(undefined) ).toBe( "" );
	} );

	it( 'names Good itself, and falls back to the severity and the code while a name is on its way', ()=>{
		expect( statusText(0) ).toBe( "Good" );
		expect( statusText(undefined) ).toBe( "Good" );
		expect( statusText(0x00000480) ).toBe( "Good+Overflow" );
		expect( statusText(0x40940600) ).toBe( "Uncertain 0x40940000+High" );
		expect( statusText(0x808C0000) ).toBe( "Bad 0x808C0000" );
		expect( statusText(0x40940600, "UncertainEngineeringUnitsExceeded") ).toBe( "UncertainEngineeringUnitsExceeded+High" );
	} );

	it( 'pads the hex to the eight digits of a uint32', ()=>{
		expect( scHex(0) ).toBe( "0x00000000" );
		expect( scHex(0x960000) ).toBe( "0x00960000" );
		expect( scHex(0x808C0000) ).toBe( "0x808C0000" );
	} );

	it( 'picks a shape per severity, and none for plain Good, calculated or not', ()=>{
		expect( statusIcon(0) ).toBeUndefined();
		expect( statusIcon(undefined) ).toBeUndefined();
		expect( statusIcon(0x00000401) ).toBeUndefined();//an aggregate's Good
		expect( statusIcon(0x00000405) ).toBe( "info" );//a partial interval's
		expect( statusIcon(0x00960000) ).toBe( "info" );
		expect( statusIcon(0x00000480) ).toBe( "info" );
		expect( statusIcon(0x40940600) ).toBe( "warning" );
		expect( statusIcon(0x808C0000) ).toBe( "error" );
	} );
} );

//the name cache is static, so each test here keeps to codes of its own.
describe( 'OpcError status names', ()=>{
	//A name that arrived after the first paint repainted nothing, so the History tab threaded a counter through three components
	//to force it (historian-web-trend #14).  text() reads a version setMessages bumps.
	it( 'repaints what shows a code when its name arrives', ()=>{
		const shown = computed( ()=>OpcError.text(0x80EE0000) );
		expect( shown() ).toBe( "Bad 0x80EE0000" );
		OpcError.setMessages( [{sc: 0x80EE0000, message: "BadNamedForTheTest"}] );
		expect( shown() ).toBe( "BadNamedForTheTest" );
	} );
	it( 'asks for one name however many flag variants of the code turn up', ()=>{
		OpcError.text( 0x40920500 );
		OpcError.text( 0x40920600 );
		expect( OpcError.emptyMessages().filter(sc=>sc==0x40920000) ).toHaveLength( 1 );
		expect( OpcError.emptyMessages() ).not.toContain( 0x40920500 );
	} );

	it( 'never asks for Good', ()=>{
		expect( OpcError.text(0x00000480) ).toBe( "Good+Overflow" );
		expect( OpcError.emptyMessages() ).not.toContain( 0 );
	} );

	it( 'shows the server\'s name with the flags once it has arrived', ()=>{
		expect( OpcError.text(0x408F0600) ).toBe( "Uncertain 0x408F0000+High" );
		OpcError.setMessages( [{sc: 0x408F0000, message: "UncertainSubstituteValue"}] );
		expect( OpcError.text(0x408F0600) ).toBe( "UncertainSubstituteValue+High" );
		expect( OpcError.text(0x408F0000) ).toBe( "UncertainSubstituteValue" );
	} );

	it( 'files a name the server answered under a flagged code', ()=>{
		OpcError.setMessages( [{sc: 0x808A0600, message: "BadNotConnected"}] );
		expect( OpcError.statusCodeText(0x808A0000) ).toBe( "BadNotConnected" );
	} );

	it( 'never renders an unfetched name as "undefined" or "null"', ()=>{
		const text = new OpcError( 0x808D0000, "OpcError", "", undefined ).toString();
		expect( text ).toBe( "[808d0000] - Bad 0x808D0000" );
	} );
} );
