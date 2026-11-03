import { TestBed } from '@angular/core/testing';
import { MAT_DIALOG_DATA, MatDialogRef } from '@angular/material/dialog';
import { DiscoveredEndpoint, DiscoveredServer, discoverEndpointsMutation, discoverServersMutation, endpointPick, serverUrl } from '../../../model/discovery';
import { DiscoverDialog, DiscoverDialogData } from './discover-dialog';
import { flush } from 'jde-spa/testing';

//#414 (docs/clients/spec.md *Discovery*):  the servers the gateway finds at the url typed, then the endpoints of the one picked,
//and a pick closes with what fills the connection form.
const plc = ( over:Partial<DiscoveredServer>={} ):DiscoveredServer=>({ applicationUri: 'urn:plc', productUri: '', applicationName: 'PLC 1', applicationType: 'Server', gatewayServerUri: '', discoveryProfileUri: '', discoveryUrls: ['opc.tcp://plc:4840'], ...over });
const lds = plc( {applicationUri: 'urn:lds', applicationName: 'LDS', applicationType: 'DiscoveryServer', discoveryUrls: ['opc.tcp://host:4840']} );
const none:DiscoveredEndpoint = { url: 'opc.tcp://plc:4840', applicationUri: 'urn:plc', policy: 'http://opcfoundation.org/UA/SecurityPolicy#None', mode: 'None', securityLevel: 0, tokenPolicies: [{type: 'Anonymous', policy: 'http://opcfoundation.org/UA/SecurityPolicy#None', policyId: 'open62541-anonymous-policy'}], certificate: null };
const secured:DiscoveredEndpoint = { url: 'opc.tcp://plc:4840', applicationUri: 'urn:plc', policy: 'http://opcfoundation.org/UA/SecurityPolicy#Basic256Sha256', mode: 'SignAndEncrypt', securityLevel: 3,
	tokenPolicies: [{type: 'Anonymous', policy: 'http://opcfoundation.org/UA/SecurityPolicy#Basic256Sha256', policyId: 'a'}, {type: 'Username', policy: 'http://opcfoundation.org/UA/SecurityPolicy#Basic256Sha256', policyId: 'u'}],
	certificate: {subject: 'CN=plc', issuer: 'CN=plc', notBefore: '2026-01-01T00:00:00Z', notAfter: '2027-01-01T00:00:00Z', uri: 'urn:plc', thumbprint: 'AB'.repeat( 20 )} };
type Answers = { servers?:( url:string )=>DiscoveredServer[]|Promise<DiscoveredServer[]>; endpoints?:( url:string )=>DiscoveredEndpoint[]|Promise<DiscoveredEndpoint[]> };
//the dialog over a gateway whose discover answers from `answers`, by the mutation sent
const open = ( url:string, answers:Answers={} )=>{
	const close = vi.fn( ( _y:unknown )=>{} );
	const postQL = vi.fn( async ( ql:string, vars:{url:string} )=>{
		if( ql==discoverServersMutation )
			return { discover: await (answers.servers ?? (()=>[plc()]))( vars.url ) };
		if( ql==discoverEndpointsMutation )
			return { discover: await (answers.endpoints ?? (()=>[none, secured]))( vars.url ) };
		throw new Error( `unexpected ${ql}` );
	} );
	TestBed.configureTestingModule({ providers: [
		{ provide: MatDialogRef, useValue: { close } },
		{ provide: MAT_DIALOG_DATA, useValue: <DiscoverDialogData>{ gateway: <any>{postQL}, url } }
	]});
	const fixture = TestBed.createComponent( DiscoverDialog );
	fixture.detectChanges();
	const settle = async ()=>{ await flush( 4 ); fixture.detectChanges(); };
	const texts = ( selector:string )=>Array.from( (fixture.nativeElement as HTMLElement).querySelectorAll( selector ) ).map( e=>e.textContent!.trim() );
	return { fixture, dialog: fixture.componentInstance, postQL, closed: ()=>close.mock.calls.map( ([y])=>y ), settle, texts, sent: ()=>postQL.mock.calls.map( ([ql, vars])=>[ql==discoverServersMutation ? 'servers' : 'endpoints', vars.url] ) };
};

describe( 'DiscoverDialog', ()=>{
	it( 'finds the servers at the url and opens the one server found', async ()=>{
		const d = open( ' opc.tcp://plc:4840 ' );
		await d.settle();
		expect( d.sent() ).toEqual( [['servers', 'opc.tcp://plc:4840'], ['endpoints', 'opc.tcp://plc:4840']] );//trimmed;  the server's own discovery url
		expect( d.dialog.server()?.applicationUri ).toBe( 'urn:plc' );
		expect( d.dialog.endpoints().map( e=>e.mode ) ).toEqual( ['SignAndEncrypt', 'None'] );//the server's ranking, strongest first
		expect( d.texts( 'mat-table.endpoints mat-row' ).length ).toBe( 2 );
		expect( d.dialog.error() ).toBeUndefined();
		expect( d.dialog.loading() ).toBe( false );
	} );

	it( 'lists several servers and picks one, with Back to the list', async ()=>{
		const d = open( 'opc.tcp://host:4840', {servers: ()=>[lds, plc()]} );
		await d.settle();
		expect( d.dialog.server() ).toBeUndefined();
		expect( d.texts( 'mat-table.servers mat-row .open' ) ).toEqual( ['LDS', 'PLC 1'] );
		expect( d.sent() ).toEqual( [['servers', 'opc.tcp://host:4840']] );
		await d.dialog.pick( plc() );
		await d.settle();
		expect( d.sent()[1] ).toEqual( ['endpoints', 'opc.tcp://plc:4840'] );
		expect( d.dialog.endpoints().length ).toBe( 2 );
		d.dialog.back();
		expect( d.dialog.server() ).toBeUndefined();
		expect( d.dialog.endpoints() ).toEqual( [] );
		expect( d.dialog.servers().length ).toBe( 2 );
	} );

	it( 'closes with what fills the form when an endpoint is chosen', async ()=>{
		const d = open( 'opc.tcp://plc:4840' );
		await d.settle();
		d.dialog.choose( secured );
		expect( d.closed() ).toEqual( [{ url: 'opc.tcp://plc:4840', certificateUri: 'urn:plc', securityMode: 'SignAndEncrypt', securityPolicy: 'http://opcfoundation.org/UA/SecurityPolicy#Basic256Sha256' }] );
		expect( endpointPick( none ).securityPolicy ).toBe( 'http://opcfoundation.org/UA/SecurityPolicy#None' );
	} );

	//a server advertises the urls it was configured with, which the gateway may not reach:  the typed one is offered in their place
	it( 'offers the typed url where the server advertises another', async ()=>{
		const d = open( 'opc.tcp://10.0.0.5:4840', {servers: ()=>[plc( {discoveryUrls: ['opc.tcp://plc.internal:4840']} )], endpoints: url=>{ if( url!='opc.tcp://10.0.0.5:4840' ) throw new Error( `'${url}' did not resolve within 3 s.` ); return [none]; }} );
		await d.settle();
		expect( d.dialog.endpointsAt() ).toBe( 'opc.tcp://plc.internal:4840' );
		expect( d.dialog.error() ).toContain( 'did not resolve' );
		expect( d.dialog.typedInstead() ).toBe( 'opc.tcp://10.0.0.5:4840' );
		await d.dialog.tryTyped();
		await d.settle();
		expect( d.dialog.error() ).toBeUndefined();
		expect( d.dialog.endpoints().length ).toBe( 1 );
		expect( d.dialog.typedInstead() ).toBeUndefined();
		expect( serverUrl( plc( {discoveryUrls: ['https://plc/', 'opc.tcp://plc:4840']} ), 'typed' ) ).toBe( 'opc.tcp://plc:4840' );//the first opc.tcp one
		expect( serverUrl( plc( {discoveryUrls: []} ), 'typed' ) ).toBe( 'typed' );
	} );

	it( 'shows the gateway\'s refusal and tries another url', async ()=>{
		let fail = true;
		const d = open( 'opc.tcp://nowhere:4840', {servers: ()=>{ if( fail ) throw new Error( "(502)'opc.tcp://nowhere:4840' did not answer FindServers" ); return [plc()]; }} );
		await d.settle();
		expect( d.dialog.error() ).toContain( 'did not answer FindServers' );
		expect( d.texts( '.refused' ).length ).toBe( 1 );
		fail = false;
		d.dialog.url.set( 'opc.tcp://plc:4840' );
		await d.dialog.findServers();
		await d.settle();
		expect( d.dialog.error() ).toBeUndefined();
		expect( d.dialog.server()?.applicationName ).toBe( 'PLC 1' );
	} );

	it( 'says what each endpoint takes and carries', async ()=>{
		const d = open( 'opc.tcp://plc:4840' );
		await d.settle();
		expect( d.dialog.tokens( secured ) ).toBe( 'Anonymous, Username' );
		expect( d.dialog.tokensTitle( secured ) ).toBe( 'Anonymous@Basic256Sha256, Username@Basic256Sha256' );
		expect( d.dialog.certificateText( secured ) ).toBe( 'urn:plc' );
		expect( d.dialog.certificateTitle( secured ) ).toContain( 'Thumbprint ' + 'AB'.repeat( 20 ) );
		expect( d.dialog.certificateText( none ) ).toBe( 'none' );
		expect( d.dialog.certificateText( {...none, certificate: {error: 'bad DER'} as any} ) ).toBe( 'could not be read' );
		expect( d.dialog.policyName( secured ) ).toBe( 'Basic256Sha256' );
		expect( d.dialog.modeName( secured ) ).toBe( 'Sign & Encrypt' );
	} );

	it( 'discovers nothing from an empty url', async ()=>{
		const d = open( '  ' );
		await d.settle();
		expect( d.sent() ).toEqual( [] );
		expect( d.dialog.canFind() ).toBe( false );
		expect( d.texts( '.empty' ) ).toEqual( ['Enter a URL to discover.'] );
	} );
} );