import { TestBed } from '@angular/core/testing';
import { SnackbarService } from 'jde-framework';
import { snackbarStub } from 'jde-framework/testing';
import { flush } from 'jde-spa/testing';
import { fakeGateway, TestGateway, variable } from 'jde-opc/testing';
import { Attribute, attributesOf, EWriteMask } from '../../../model/attribute';
import { ENodeClass, OpcObject, UaNode } from '../../../model/node';
import { NodeId } from '../../../model/node-id';
import { NodeView } from '../../../model/node-view';
import { NodeAttributes } from './node-attributes';

const badAttributeIdInvalid = 0x80350000;
//pumpManual's attributes as the gateway answers `node( opc, id ){ … }`:  the permissions refused, the rest answered;  `userWriteMask` as given
const objectJson = ( userWriteMask?:number )=>({ id: {ns: 2, i: 5005}, nodeClass: 1, browse: {ns: 2, name: 'pumpManual'}, name: 'Pump (Manual)', description: 'Explicit object without a type.', writeMask: 96, userWriteMask, rolePermissions: {sc: badAttributeIdInvalid}, userRolePermissions: {sc: badAttributeIdInvalid}, accessRestrictions: {sc: badAttributeIdInvalid}, eventNotifier: 0 });
//motorRpm's, the Value an Uncertain reading with both timestamps
const variableJson = { id: {ns: 2, i: 6012}, nodeClass: 2, browse: {ns: 2, name: 'motorRpm'}, name: 'motorRpm', description: null, writeMask: 0, userWriteMask: 0, rolePermissions: {sc: badAttributeIdInvalid}, userRolePermissions: {sc: badAttributeIdInvalid}, accessRestrictions: {sc: badAttributeIdInvalid}, dataValue: {v: 1450.5, sc: 0x40900000, source: {seconds: 1762300000, nanos: 5000000}, server: {seconds: 1762300001, nanos: 0}}, dataType: {i: 11}, valueRank: -1, arrayDimensions: [], accessLevel: 3, userAccessLevel: 3, accessLevelEx: 3, minimumSamplingInterval: 0, historizing: true };
const pumpManual = new OpcObject( {ns: 2, i: 5005, name: 'Pump (Manual)', browse: {ns: 2, name: 'pumpManual'}} );

describe( 'NodeAttributes', ()=>{
	//the tab over a gateway that answers the attributes from `json` - as Gateway.attributes builds them - and echoes a write
	const create = ( node:UaNode, json:any )=>{
		const gateway = Object.assign( fakeGateway( 'gw' ), {
			attributes: vi.fn( async ( _opc:string, n:UaNode )=>attributesOf( n.nodeClass ).map( d=>new Attribute( d, json[d.key] ) ) ),
			updateNode: vi.fn( async ( _opc:string, _id:NodeId, attributes:Record<string,unknown>, _columns:string[] )=>({ ...attributes, userWriteMask: json.userWriteMask }) )
		} );
		const snackbar = snackbarStub();
		TestBed.configureTestingModule({ providers: [{ provide: SnackbarService, useValue: snackbar }] });
		const fixture = TestBed.createComponent( NodeAttributes );
		fixture.componentRef.setInput( 'pageData', {gateway, server: {connection: {slug: 'cn'}}} );
		fixture.componentRef.setInput( 'node', node );
		fixture.detectChanges();
		return { fixture, page: fixture.componentInstance, gateway, snackbar };
	};
	const row = ( page:NodeAttributes, key:string )=>page.rows().find( a=>a.key==key )!;

	it( "lists the attributes of the node's class, each with its status, the Value with its quality and both timestamps", async ()=>{
		const { fixture, page, gateway } = create( variable( 6012, 'motorRpm' ), variableJson );
		await flush( 2 );
		fixture.detectChanges();
		expect( gateway.attributes ).toHaveBeenCalledWith( 'cn', expect.objectContaining( {ns: 2, id: 6012} ) );
		expect( page.rows().map( a=>a.label ) ).toEqual( attributesOf( ENodeClass.Variable ).map( d=>d.label ) );
		expect( page.rows().map( a=>a.label ) ).toContain( 'Historizing' );
		expect( page.rows().map( a=>a.label ) ).not.toContain( 'Executable' );
		const value = row( page, 'dataValue' );
		expect( value.value ).toBe( 1450.5 );
		expect( value.sc ).toBe( 0x40900000 );
		expect( page.status( value ) ).toMatch( /^Uncertain/ );
		expect( page.time( value.source ) ).toBe( NodeView.time( new Date( 1762300000005 ) ) );
		expect( page.time( value.server ) ).toBe( NodeView.time( new Date( 1762300001000 ) ) );
		const permissions = row( page, 'rolePermissions' );
		expect( permissions.refused ).toBe( true );
		expect( permissions.text ).toBe( '' );
		expect( page.status( permissions ) ).toMatch( /^Bad/ );
		expect( page.status( row( page, 'name' ) ) ).toBe( '' );//answered:  nothing to say
		expect( [row( page, 'id' ).text, row( page, 'nodeClass' ).text, row( page, 'browse' ).text, row( page, 'dataType' ).text, row( page, 'historizing' ).text] ).toEqual( ['ns=2;i=6012', 'Variable', '2:motorRpm', 'Double', 'true'] );
		const cells = [...fixture.nativeElement.querySelectorAll( 'mat-row' )].map( r=>[...r.querySelectorAll( 'mat-cell' )].map( c=>c.textContent.trim() ) );
		expect( cells.find( c=>c[0]=='Value' )?.slice( 0, 2 ) ).toEqual( ['Value', '1450.5'] );
		expect( fixture.nativeElement.querySelectorAll( 'input, mat-checkbox' ).length ).toBe( 0 );//a mask of 0:  nothing to edit
	} );

	//the edit gate:  an editor where the UserWriteMask has the bit, and none where it has not, or where the server gave no mask
	it( 'offers an editor for the attributes the UserWriteMask allows', async ()=>{
		const { fixture, page } = create( pumpManual, objectJson( EWriteMask.Description ) );
		await flush( 2 );
		fixture.detectChanges();
		expect( page.editor( row( page, 'description' ) ) ).toBe( 'text' );
		expect( page.editor( row( page, 'name' ) ) ).toBeUndefined();
		expect( page.editor( row( page, 'writeMask' ) ) ).toBeUndefined();
		const inputs = [...fixture.nativeElement.querySelectorAll( 'input' )];
		expect( inputs.map( i=>i.getAttribute( 'aria-label' ) ) ).toEqual( ['Description'] );
		expect( inputs[0].value ).toBe( 'Explicit object without a type.' );
	} );
	it( 'offers no editor where the server gave no UserWriteMask', async ()=>{
		const { fixture, page } = create( pumpManual, objectJson( undefined ) );
		await flush( 2 );
		fixture.detectChanges();
		expect( page.userWriteMask() ).toBeUndefined();
		expect( page.editor( row( page, 'description' ) ) ).toBeUndefined();
		expect( fixture.nativeElement.querySelectorAll( 'input, mat-checkbox' ).length ).toBe( 0 );
	} );

	it( 'writes an attribute through updateNode and shows what the server reads back', async ()=>{
		const { fixture, page, gateway } = create( pumpManual, objectJson( EWriteMask.Description|EWriteMask.WriteMask ) );
		await flush( 2 );
		fixture.detectChanges();
		const box = <HTMLInputElement>fixture.nativeElement.querySelector( 'input[aria-label="Description"]' );
		box.value = 'A pump to try the Attributes tab on.';
		box.dispatchEvent( new Event( 'change' ) );
		await flush( 2 );
		expect( gateway.updateNode ).toHaveBeenCalledWith( 'cn', expect.objectContaining( {ns: 2, id: 5005} ), {description: 'A pump to try the Attributes tab on.'}, ['description', 'userWriteMask'] );
		expect( row( page, 'description' ).text ).toBe( 'A pump to try the Attributes tab on.' );
		expect( gateway.attributes ).toHaveBeenCalledTimes( 1 );//the echo is the reading:  no re-read
		const mask = <HTMLInputElement>fixture.nativeElement.querySelector( 'input[aria-label="WriteMask"]' );
		mask.value = '';
		mask.dispatchEvent( new Event( 'change' ) );
		await flush( 2 );
		expect( gateway.updateNode ).toHaveBeenCalledTimes( 1 );//a cleared box writes nothing
		expect( mask.value ).toBe( '96' );
	} );

	it( 'says so and reads the attributes again when the server refuses a write', async ()=>{
		const { fixture, page, gateway, snackbar } = create( pumpManual, objectJson( EWriteMask.Description ) );
		await flush( 2 );
		fixture.detectChanges();
		gateway.updateNode.mockRejectedValue( new Error( 'BadUserAccessDenied' ) );
		const box = <HTMLInputElement>fixture.nativeElement.querySelector( 'input[aria-label="Description"]' );
		box.value = 'refused';
		box.dispatchEvent( new Event( 'change' ) );
		await flush( 3 );
		expect( snackbar.exception ).toHaveBeenCalledWith( 'Could not write Description.', expect.any( Error ) );
		expect( gateway.attributes ).toHaveBeenCalledTimes( 2 );
		expect( row( page, 'description' ).text ).toBe( 'Explicit object without a type.' );
	} );

	it( 'shows the error when the gateway cannot read them', async ()=>{
		const { fixture, page, gateway } = create( pumpManual, objectJson( 0 ) );
		gateway.attributes.mockRejectedValue( new Error( 'no session' ) );
		fixture.componentRef.setInput( 'node', variable( 6012, 'motorRpm' ) );
		fixture.detectChanges();
		await flush( 2 );
		fixture.detectChanges();
		expect( page.rows() ).toEqual( [] );
		expect( page.error() ).toContain( 'no session' );
		expect( fixture.nativeElement.querySelector( '[role="alert"]' )?.textContent ).toContain( 'Could not read the attributes.' );
	} );

	//Gateway.attributes asks the class's columns in one read and names the statuses;  updateNode answers the columns read back
	it( 'reads the attributes of the class through the gateway, and writes through updateNode', async ()=>{
		const gateway = new TestGateway();
		gateway.qlResult = { node: objectJson( 96 ) };
		const rows = await gateway.attributes( 'cn', pumpManual );
		expect( rows.map( a=>a.key ) ).toEqual( attributesOf( ENodeClass.Object ).map( d=>d.key ) );
		expect( rows.find( a=>a.key=='description' )?.text ).toBe( 'Explicit object without a type.' );
		expect( rows.find( a=>a.key=='userWriteMask' )?.tooltip ).toBe( 'Description, DisplayName' );
		expect( gateway.gets ).toHaveBeenCalled();//the names of the refused reads' statuses
		gateway.posted = { data: {updateNode: {description: 'written', userWriteMask: 96}} };
		expect( await gateway.updateNode( 'cn', pumpManual.nodeId, {description: 'written'}, ['description', 'userWriteMask'] ) ).toEqual( {description: 'written', userWriteMask: 96} );
	} );
} );