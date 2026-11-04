import { TestBed } from '@angular/core/testing';
import { MAT_DIALOG_DATA, MatDialogRef } from '@angular/material/dialog';
import { CallResult, MethodArguments } from '../../../model/method';
import { Method, OpcObject } from '../../../model/node';
import { StatusCode } from '../../../model/types';
import { Value } from '../../../model/value';
import { MethodCallDialog, MethodCallDialogData } from './method-call-dialog';

const pump = new OpcObject( <any>{ns: 2, i: 5005, name: 'Pump (Manual)', browse: {ns: 2, name: 'pumpManual'}} );
const toHertz = new Method( <any>{ns: 2, i: 6055, name: 'toHertz', browse: {ns: 2, name: 'toHertz'}, description: {text: 'Converts rpm to Hz.'}}, pump );
const declared = new MethodArguments( {name: 'toHertz',
	inputArguments: [{name: 'rpm', dataType: {i: 11}, valueRank: -1, description: 'The motor speed, in rpm.'}],
	outputArguments: [{name: 'hertz', dataType: {i: 11}}, {name: 'text', dataType: {i: 12}}]} );
const BadInvalidArgument = 0x80AB0000, BadTypeMismatch = 0x80740000, BadUserAccessDenied = 0x801F0000;

//The dialog over a stub gateway:  `arguments` answers the method's declaration, `calls` is what it sent, `answerWith` sets the
//result each call gets, and `names` the status codes it asked the names of.
const open = async ( data:Partial<MethodCallDialogData>={} )=>{
	let answer:CallResult|Error = { statusCode: 0, inputArgumentResults: [], outputArguments: [] };
	const call = vi.fn( async ( _args:Value[] )=>{ if( answer instanceof Error ) throw answer; return answer; } );
	const names = vi.fn( ( _codes:StatusCode[] )=>{} );
	const ref = { disableClose: <boolean|undefined>undefined, close: vi.fn( ( _r?:CallResult )=>{} ) };
	TestBed.resetTestingModule();//a second dialog in one test
	TestBed.configureTestingModule({ providers: [
		{ provide: MatDialogRef, useValue: ref },
		{ provide: MAT_DIALOG_DATA, useValue: <MethodCallDialogData>{ object: pump, method: toHertz, arguments: async ()=>declared, call, names, ...data } }
	]});
	const fixture = TestBed.createComponent( MethodCallDialog );
	const dialog = fixture.componentInstance;
	await dialog.load();
	fixture.detectChanges();
	return { fixture, dialog, ref,
		calls: ()=>call.mock.calls.map( ([args])=>args ),
		names: ()=>names.mock.calls.map( ([codes])=>codes ),
		answerWith: ( a:CallResult|Error )=>{ answer = a; } };
};

describe( 'MethodCallDialog', ()=>{
	it( 'builds an editor per input argument from the declaration, and sends nothing until each has a value', async ()=>{
		const d = await open();
		expect( d.dialog.title ).toBe( 'Pump (Manual).toHertz' );
		expect( d.dialog.inputs.map( a=>a.editor ) ).toEqual( ['number'] );
		expect( d.dialog.label( d.dialog.inputs[0] ) ).toBe( 'rpm (Double)' );
		expect( d.dialog.problem() ).toBe( 'Enter rpm.' );
		await d.dialog.apply();
		expect( d.calls() ).toHaveLength( 0 );
		expect( d.fixture.nativeElement.textContent ).toContain( 'Converts rpm to Hz.' );
	} );
	it( 'calls with the arguments typed and shows the status and the outputs by name', async ()=>{
		const d = await open();
		d.dialog.setText( 0, '1500' );
		expect( d.dialog.problem() ).toBeUndefined();
		d.answerWith( {statusCode: 0, inputArgumentResults: [], outputArguments: [25, '25 Hz']} );
		await d.dialog.apply();
		expect( d.calls() ).toEqual( [[1500]] );
		expect( d.dialog.outputs() ).toEqual( [{name: 'hertz', text: '25'}, {name: 'text', text: '25 Hz'}] );
		expect( d.dialog.argumentResults() ).toEqual( [] );
		expect( d.names() ).toEqual( [] );//Good needs no name
		expect( d.ref.close ).not.toHaveBeenCalled();//open for another go
		expect( d.ref.disableClose ).toBe( false );
	} );
	it( 'shows a refusal with each argument\'s status, by name once fetched', async ()=>{
		const d = await open();
		d.dialog.setText( 0, '1500' );
		d.answerWith( {statusCode: BadInvalidArgument, inputArgumentResults: [BadTypeMismatch], outputArguments: []} );
		await d.dialog.apply();
		expect( d.dialog.argumentResults() ).toEqual( [{name: 'rpm', status: BadTypeMismatch}] );
		expect( d.dialog.outputs() ).toEqual( [] );
		expect( d.names() ).toEqual( [[BadInvalidArgument, BadTypeMismatch]] );
		d.answerWith( {statusCode: BadUserAccessDenied, inputArgumentResults: [], outputArguments: []} );
		await d.dialog.apply();
		expect( d.names().at( -1 ) ).toEqual( [BadUserAccessDenied] );
	} );
	it( 'shows the gateway\'s refusal before any call, and the arguments\' when they cannot be read', async ()=>{
		const d = await open();
		d.dialog.setText( 0, '1500' );
		d.answerWith( new Error( 'BadNodeIdUnknown' ) );
		await d.dialog.apply();
		expect( d.dialog.error() ).toBe( 'BadNodeIdUnknown' );
		expect( d.dialog.result() ).toBeUndefined();
		const unread = await open( {arguments: async ()=>{ throw new Error( 'BadNodeIdUnknown' ); }} );
		expect( unread.dialog.loadError() ).toBe( 'BadNodeIdUnknown' );
		expect( unread.dialog.problem() ).toBe( 'The arguments could not be read.' );
	} );
	it( 'takes a method with no arguments, and a toggle for a Boolean', async ()=>{
		const none = await open( {arguments: async ()=>new MethodArguments( {name: 'reset'} )} );
		expect( none.dialog.problem() ).toBeUndefined();
		await none.dialog.apply();
		expect( none.calls() ).toEqual( [[]] );
		const flag = await open( {arguments: async ()=>new MethodArguments( {name: 'run', inputArguments: [{name: 'on', dataType: {i: 1}}, {name: 'label', dataType: {i: 12}}]} )} );
		expect( flag.dialog.inputs.map( a=>a.editor ) ).toEqual( ['boolean', 'text'] );
		expect( flag.dialog.problem() ).toBeUndefined();//a toggle holds false, and text may be blank
		flag.dialog.setFlag( 0, true );
		flag.dialog.setText( 1, 'x' );
		await flag.dialog.apply();
		expect( flag.calls() ).toEqual( [[true, 'x']] );
	} );
} );