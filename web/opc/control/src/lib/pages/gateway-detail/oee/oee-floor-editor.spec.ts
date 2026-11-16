import { TestBed } from '@angular/core/testing';
import { OeeFloor, OeeFloorRights, toOeeFloorShape } from '../../../model/oee-floor';
import { FloorBase } from '../../../model/oee-floor-edit';
import { OeePlant } from '../../../model/oee-lines';
import { OeeFloorService } from '../../../services/oee-floor-service';
import { OeeFloorEditor } from './oee-floor-editor';
import { stable } from 'jde-spa/testing';

const plant:OeePlant = {
	machines: [{id: 1, name: 'Filler', slug: 'filler'}, {id: 2, name: 'Capper', slug: 'capper'}, {id: 3, name: 'Labeler', slug: 'labeler'}, {id: 4, name: 'Packer', slug: 'packer'}],
	lines: [{id: 10, name: 'Plant', slug: 'plant', level: 'Site'}, {id: 11, name: 'Bottling', slug: 'bottling', level: 'Line'}, {id: 12, name: 'Packing', slug: 'packing', level: 'Line'}],
	members: [
		{id: 1, lineId: 10, memberLineId: 11, sort: 0}, {id: 2, lineId: 10, memberLineId: 12, sort: 1},
		{id: 3, lineId: 11, machineId: 1, sort: 0}, {id: 4, lineId: 11, machineId: 2, sort: 1}, {id: 5, lineId: 11, machineId: 3, sort: 2}, {id: 6, lineId: 12, machineId: 4, sort: 0}
	]
};
const floor:OeeFloor = { id: 1, lineId: 10, width: 40, height: 20, grid: 0.5 };
const row = ( id:number, kind:string, points:number[][], extra:object={} )=>toOeeFloorShape( { id, oeeFloorId: 1, kind, points, sort: 0, ...extra } )!;
const rect = ( x:number, y:number, w:number, h:number ):[number, number][]=>[[x, y], [x+w, y], [x+w, y+h], [x, y+h]];
const rows = ()=>[row( 1, 'Machine', rect( 3, 3, 6, 4 ), {oeeMachineId: 1} ), row( 7, 'Zone', rect( 9, 4.5, 4, 1 ), {label: 'Conveyor'} ), row( 8, 'Label', [[38, 15]], {label: 'Dock'} )];
const all:OeeFloorRights = { createFloor: true, updateFloor: true, createShape: true, updateShape: true, purgeShape: true };
const none:OeeFloorRights = { createFloor: false, updateFloor: false, createShape: false, updateShape: false, purgeShape: false };
//a client point for a point of the floor in metres:  the svg is given 820 by 420 pixels at the window's corner, so the whole floor, a grid's margin about it, is 20 pixels a metre
const at = ( x:number, y:number )=>({ clientX: ( x+0.5 )*20, clientY: ( y+0.5 )*20 });
const fire = ( el:Element, type:string, where:MouseEventInit={}, extra:object={} )=>el.dispatchEvent( Object.assign( new MouseEvent( type, {bubbles: true, cancelable: true, button: 0, ...where} ), {pointerId: 1, pointerType: 'mouse', ...extra} ) );
//The editor over a stubbed service, on the demo's floor:  the filler, a zone and a label placed, three machines and two lines not.
const create = async ( {rights=all, shapes=rows()}:{rights?:OeeFloorRights; shapes?:FloorBase['shapes']}={} )=>{
	const service = { save: vi.fn( async ( ..._:unknown[] )=>{} ), floor: vi.fn( async ( ..._:unknown[] ):Promise<FloorBase|undefined>=>undefined ) };
	const gateway = { name: 'gw' };
	TestBed.configureTestingModule( { providers: [{ provide: OeeFloorService, useValue: service }] } );
	const fixture = TestBed.createComponent( OeeFloorEditor );
	for( const [name, value] of Object.entries( { gateway, line: plant.lines[0], source: {floor, shapes}, plant, rights } ) )
		fixture.componentRef.setInput( name, value );
	const editor = fixture.componentInstance, closed:boolean[] = [];
	editor.closed.subscribe( saved=>closed.push( saved ) );
	await stable( fixture );
	const el = ()=><HTMLElement>fixture.nativeElement, svg = ()=>el().querySelector( 'svg' )!;
	Object.assign( svg(), { getBoundingClientRect: ()=>({ left: 0, top: 0, right: 820, bottom: 420, width: 820, height: 420 }) } );
	editor.port.size.set( {width: 820, height: 420} );
	await stable( fixture );
	const labelled = ( selector:string, label:string )=>[...el().querySelectorAll( selector )].find( e=>e.getAttribute( 'aria-label' )==label )!;
	const shape = ( title:string )=>labelled( 'g.shape', title ), grip = ( label:string )=>labelled( 'g.grip', label );
	const grips = ()=>[...el().querySelectorAll( 'g.grip' )].map( g=>g.getAttribute( 'aria-label' )! );
	const offers = ()=>[...el().querySelectorAll( '.offer .mdc-button__label' )].map( label=>label.textContent!.trim() );//its text, the icon's ligature left out
	const offer = ( label:string )=>[...el().querySelectorAll( '.offer' )].find( b=>b.textContent!.trim().endsWith( label ) )!;
	const button = ( label:string )=><HTMLButtonElement>[...el().querySelectorAll( 'button' )].find( b=>b.getAttribute( 'aria-label' )==label || b.textContent!.trim().endsWith( label ) )!;
	const points = ( key:string )=>editor.edit().shapes.find( s=>s.key==key )?.points;
	const key = async ( on:Element, name:string, extra:object={} )=>{
		on.dispatchEvent( new KeyboardEvent( 'keydown', {key: name, bubbles: true, cancelable: true, ...extra} ) );
		await stable( fixture );
	};
	//a press at `from`, a drag to `to` and the release there, each on the svg once the press is taken
	const drag = async ( on:Element, from:{clientX:number; clientY:number}, to:{clientX:number; clientY:number} )=>{
		fire( on, 'pointerdown', from );
		fire( svg(), 'pointermove', to );
		fire( svg(), 'pointerup', to );
		await stable( fixture );
	};
	const said = ()=>[...el().querySelectorAll( '.said' )].map( p=>p.textContent!.trim() );
	const greyed = ( b:Element )=>b.getAttribute( 'aria-disabled' )=='true' || ( <HTMLButtonElement>b ).disabled;
	const type = async ( field:number, text:string )=>{
		const input = el().querySelectorAll<HTMLInputElement>( '.sizes input' )[field];
		input.value = text;
		input.dispatchEvent( new Event( 'input' ) );
		await stable( fixture );
	};
	return { editor, fixture, service, gateway, closed, el, svg, shape, grip, grips, offers, offer, button, points, key, drag, said, greyed, type };
};

describe( 'OeeFloorEditor', ()=>{
	afterEach( ()=>vi.restoreAllMocks() );
	it( 'places what is dragged from a list as a rectangle on the grid, and what is pressed at the middle of the view', async ()=>{
		const p = await create();
		expect( p.offers() ).toEqual( ['Capper', 'Labeler', 'Packer', 'Bottling, Line', 'Packing, Line', 'Zone', 'Wall', 'Aisle', 'Label'] );
		const capper = p.offer( 'Capper' );
		fire( capper, 'pointerdown', {clientX: 900, clientY: 50} );
		fire( capper, 'pointermove', at( 20.3, 9.8 ) );
		p.fixture.detectChanges();
		expect( p.el().querySelector( 'polygon.ghost' )?.getAttribute( 'points' ) ).toBe( '18.5,8.5 22.5,8.5 22.5,11.5 18.5,11.5' );//where it would stand
		fire( capper, 'pointerup', at( 20.3, 9.8 ) );
		fire( capper, 'click', {detail: 1} );//the pointer's own click, after its release, places nothing more
		await stable( p.fixture );
		expect( p.el().querySelector( '.ghost' ) ).toBeNull();
		expect( p.editor.edit().shapes.length ).toBe( 4 );
		expect( p.editor.edit().shapes.at( -1 ) ).toEqual( {key: 'n1', kind: 'Machine', machineId: 2, points: rect( 18.5, 8.5, 4, 3 ), sort: 1} );//4 by 3 about 20.3, 9.8, its corner on the half metre
		expect( p.editor.selected() ).toBe( 'n1' );
		expect( document.activeElement ).toBe( p.shape( 'Capper' ) );//the focus is its, so the arrows, R and Delete are
		expect( p.offers().slice( 0, 2 ) ).toEqual( ['Labeler', 'Packer'] );//placed, so no longer offered
		const labeler = p.offer( 'Labeler' );
		fire( labeler, 'pointerdown', {clientX: 900, clientY: 50} );
		fire( labeler, 'pointermove', {clientX: 900, clientY: 300} );
		fire( labeler, 'pointerup', {clientX: 900, clientY: 300} );//let go beside the floor
		fire( labeler, 'click', {detail: 1} );
		await stable( p.fixture );
		expect( p.editor.edit().shapes.length ).toBe( 4 );
		const zone = p.offer( 'Zone' );
		fire( zone, 'pointerdown', {clientX: 900, clientY: 400} );//a press that stays put:  the middle of the view, 20, 10
		fire( zone, 'pointerup', {clientX: 901, clientY: 400} );
		fire( zone, 'click', {detail: 1} );
		await stable( p.fixture );
		expect( p.editor.edit().shapes.length ).toBe( 5 );
		expect( p.editor.edit().shapes.at( -1 ) ).toEqual( {key: 'n2', kind: 'Zone', points: rect( 18, 9, 4, 2 ), sort: 1} );
		( <HTMLElement>p.offer( 'Bottling, Line' ) ).click();//Enter, or assistive technology:  a click with no pointer
		( <HTMLElement>p.offer( 'Label' ) ).click();
		expect( p.editor.edit().shapes.slice( -2 ).map( s=>[s.kind, s.lineId, s.label, s.points] ) ).toEqual( [['Line', 11, undefined, rect( 14, 7, 12, 6 )], ['Label', undefined, 'Label', [[20, 10]]]] );
	} );
	it( 'selects a shape by a press, moves it by a drag and resizes it by a handle, keeping the opposite corner', async ()=>{
		const p = await create();
		fire( p.shape( 'Filler' ), 'pointerdown', at( 5, 5 ) );
		expect( p.editor.selected() ).toBe( 's1' );
		fire( p.svg(), 'pointermove', at( 5.25, 5 ) );//five pixels, half a grid:  a press, not yet a drag
		expect( p.points( 's1' ) ).toEqual( rect( 3, 3, 6, 4 ) );
		fire( p.svg(), 'pointermove', at( 6.3, 4.8 ) );
		fire( p.svg(), 'pointerup', at( 6.3, 4.8 ) );
		expect( p.points( 's1' ) ).toEqual( rect( 4.5, 3, 6, 4 ) );//1.3 right and 0.2 up, in metres, to the grid
		await stable( p.fixture );
		expect( p.shape( 'Filler' ).getAttribute( 'aria-pressed' ) ).toBe( 'true' );
		expect( p.grips() ).toEqual( ['top left corner', 'top side', 'top right corner', 'right side', 'bottom right corner', 'bottom side', 'bottom left corner', 'left side'].map( h=>`Resize by the ${h}` ) );
		await p.drag( p.grip( 'Resize by the bottom right corner' ), at( 10.5, 7 ), at( 12.7, 8.1 ) );
		expect( p.points( 's1' ) ).toEqual( rect( 4.5, 3, 8, 5 ) );//its top left corner stays
		await p.drag( p.grip( 'Resize by the left side' ), at( 4.5, 5.5 ), at( 2.2, 9 ) );
		expect( p.points( 's1' ) ).toEqual( rect( 2, 3, 10.5, 5 ) );//a side's handle, its side alone
		await p.drag( p.shape( 'Label Dock' ), at( 38, 15 ), at( 36.2, 17.3 ) );
		expect( p.points( 's8' ) ).toEqual( [[36, 17.5]] );
		expect( p.grips() ).toEqual( [] );//a label is a point:  it moves
		await p.drag( p.svg(), at( 30, 18 ), at( 30, 18 ) );//a press on the floor that stays put
		expect( p.editor.selected() ).toBeUndefined();
		const view = p.editor.port.view();
		await p.drag( p.svg(), at( 30, 18 ), at( 31, 18 ) );//and a drag of it pans
		expect( p.editor.port.view().x ).toBeCloseTo( view.x-1 );
		expect( p.editor.edit().shapes.map( s=>s.points ) ).toEqual( [rect( 2, 3, 10.5, 5 ), rect( 9, 4.5, 4, 1 ), [[36, 17.5]]] );
	} );
	it( 'turns a rectangle, adds a point to a zone and deletes a shape, by the pointer and from the keyboard', async ()=>{
		const p = await create();
		await p.key( p.shape( 'Filler' ), 'Enter' );
		expect( p.editor.selected() ).toBe( 's1' );
		p.button( 'Turn a quarter clockwise' ).click();
		expect( p.points( 's1' ) ).toEqual( [[8, 2], [8, 8], [4, 8], [4, 2]] );
		await p.key( p.shape( 'Filler' ), 'r' );
		expect( p.points( 's1' ) ).toEqual( [[9, 7], [3, 7], [3, 3], [9, 3]] );
		await p.key( p.shape( 'Filler' ), 'ArrowRight' );
		await p.key( p.shape( 'Filler' ), 'ArrowDown', {shiftKey: true} );//its bottom side, a grid down
		expect( p.points( 's1' ) ).toEqual( [[9.5, 7.5], [3.5, 7.5], [3.5, 3], [9.5, 3]] );
		await p.drag( p.shape( 'Zone Conveyor' ), at( 11, 5 ), at( 11, 5 ) );
		expect( p.grips().length ).toBe( 8 );//a rectangle:  its box's handles
		p.button( 'Edit its points' ).click();
		await stable( p.fixture );
		expect( p.grips() ).toEqual( ['Point 1 of 4', 'Point 2 of 4', 'Point 3 of 4', 'Point 4 of 4', 'Add a point after point 1', 'Add a point after point 2', 'Add a point after point 3', 'Add a point after point 4'] );
		await p.drag( p.grip( 'Add a point after point 1' ), at( 11, 4.5 ), at( 11.2, 3.8 ) );//a side's middle is a point from its press
		expect( p.points( 's7' ) ).toEqual( [[9, 4.5], [11, 4], [13, 4.5], [13, 5.5], [9, 5.5]] );
		expect( p.grips().length ).toBe( 10 );
		await p.key( p.grip( 'Point 1 of 5' ), 'ArrowLeft' );
		expect( p.points( 's7' )![0] ).toEqual( [8.5, 4.5] );
		await p.key( p.grip( 'Point 2 of 5' ), 'Delete' );
		expect( p.points( 's7' ) ).toEqual( [[8.5, 4.5], [13, 4.5], [13, 5.5], [9, 5.5]] );
		await p.key( p.grip( 'Add a point after point 4' ), 'Enter' );//the side that closes it
		expect( p.points( 's7' )!.at( -1 ) ).toEqual( [8.75, 5] );
		( <SVGGElement>p.grip( 'Point 5 of 5' ) ).focus();
		await p.key( p.grip( 'Point 5 of 5' ), 'Delete' );
		expect( p.points( 's7' )!.length ).toBe( 4 );
		expect( document.activeElement?.getAttribute( 'aria-label' ) ).toBe( 'Point 4 of 4' );//its handle gone, the focus on the point before
		await p.key( p.shape( 'Zone Conveyor' ), 'Delete' );
		expect( p.editor.edit().shapes.map( s=>s.key ) ).toEqual( ['s1', 's8'] );
		expect( p.editor.selected() ).toBeUndefined();
		await p.key( p.shape( 'Label Dock' ), 'Enter' );
		p.button( 'Delete the shape' ).click();
		expect( p.editor.edit().shapes.map( s=>s.key ) ).toEqual( ['s1'] );
		await p.key( p.shape( 'Filler' ), 'Enter' );//a box's handle takes the arrows, and leaves the shape's other keys the shape's
		await p.key( p.grip( 'Resize by the right side' ), 'ArrowRight' );
		expect( p.points( 's1' ) ).toEqual( [[10, 7.5], [3.5, 7.5], [3.5, 3], [10, 3]] );
		await p.key( p.grip( 'Resize by the right side' ), 'r' );
		expect( p.points( 's1' ) ).toEqual( [[4.5, 8.5], [4.5, 2], [9, 2], [9, 8.5]] );
		await p.key( p.grip( 'Resize by the right side' ), 'Delete' );
		expect( p.editor.edit().shapes ).toEqual( [] );
	} );
	it( 'saves what differs as one request, and Cancel drops it', async ()=>{
		const p = await create();
		await p.key( p.shape( 'Filler' ), 'ArrowRight' );
		await p.key( p.shape( 'Label Dock' ), 'Delete' );
		( <HTMLElement>p.offer( 'Wall' ) ).click();
		await p.type( 0, '50' );
		const text = p.el().querySelector<HTMLInputElement>( '.tools input' )!;//the wall, selected as it was placed
		text.value = 'North wall ';//as it is typed, a word on its way
		text.dispatchEvent( new Event( 'input' ) );
		await stable( p.fixture );
		expect( p.greyed( p.button( 'Save' ) ) ).toBe( false );
		p.button( 'Save' ).click();
		await stable( p.fixture );
		expect( p.service.save.mock.calls ).toEqual( [[p.gateway, {
			ql: 'mutation purgeOeeFloorShape( id:$id0 ) updateOeeFloor( id:$id1, width:$width1 ) updateOeeFloorShape( id:$id2, points:$points2 ) createOeeFloorShape( oeeFloorId:$oeeFloorId3, kind:$kind3, points:$points3, label:$label3, sort:$sort3 )',
			vars: { id0: 8, id1: 1, width1: 50, id2: 1, points2: rect( 3.5, 3, 6, 4 ), oeeFloorId3: 1, kind3: 'Wall', points3: [[17, 10], [23, 10]], label3: 'North wall', sort3: 0 }
		}]] );
		expect( p.closed ).toEqual( [true] );
		p.button( 'Cancel' ).click();
		expect( p.closed ).toEqual( [true, false] );
	} );
	it( 'greys Save with the reason:  for Update alone once a shape is added, for a floor the gateway would refuse, and with nothing changed', async ()=>{
		const p = await create( {rights: {...none, updateShape: true}} );
		expect( p.greyed( p.button( 'Save' ) ) ).toBe( true );
		expect( p.editor.saveReason() ).toBe( 'Nothing is changed.' );
		expect( p.said() ).toEqual( [] );
		await p.key( p.shape( 'Filler' ), 'ArrowRight' );
		expect( p.greyed( p.button( 'Save' ) ) ).toBe( false );//a shape moved is an update
		( <HTMLElement>p.offer( 'Zone' ) ).click();
		await stable( p.fixture );
		expect( p.greyed( p.button( 'Save' ) ) ).toBe( true );
		expect( p.said() ).toEqual( ["Save needs Create on the floor's shapes (oee_floor_shapes), which your roles don't grant."] );
		p.button( 'Save' ).click();
		await stable( p.fixture );
		expect( p.service.save ).not.toHaveBeenCalled();
		await p.key( p.shape( 'Zone' ), 'Delete' );
		expect( p.greyed( p.button( 'Save' ) ) ).toBe( false );
		await p.key( p.shape( 'Label Dock' ), 'Delete' );
		expect( p.said() ).toEqual( ["Save needs Purge on the floor's shapes (oee_floor_shapes), which your roles don't grant."] );
		const granted = await ( TestBed.resetTestingModule(), create() );//a second editor, with every right
		await granted.type( 0, '8' );
		expect( granted.said() ).toEqual( ['Filler has a point at [9, 3], outside the floor, 8 by 20.'] );
		expect( granted.greyed( granted.button( 'Save' ) ) ).toBe( true );
		await granted.type( 0, '' );
		expect( granted.said() ).toEqual( ["The floor's width must be a number above zero."] );
		expect( granted.editor.edit().floor.width ).toBe( 8 );//not applied
		await granted.type( 0, '39' );
		expect( granted.editor.port.view() ).toEqual( {x: -0.5, y: -0.5, width: 40, height: 21} );//another size, fitted
		await granted.type( 2, '1' );
		expect( granted.said() ).toEqual( [] );
		expect( granted.editor.batch() ).toEqual( [{ op: 'updateFloor', args: {width: 39, grid: 1} }] );
		expect( granted.greyed( granted.button( 'Save' ) ) ).toBe( false );
	} );
	it( 'reads the rows again after a save the gateway refused part-way, and says what it said', async ()=>{
		const p = await create();
		await p.key( p.shape( 'Filler' ), 'ArrowRight' );
		( <HTMLElement>p.offer( 'Zone' ) ).click();
		await p.type( 1, '15' );
		p.service.save.mockRejectedValueOnce( new Error( 'The floor is in use.' ) );
		//the filler's update and the zone's create went in, the floor's height did not
		p.service.floor.mockResolvedValueOnce( { floor, shapes: [row( 1, 'Machine', rect( 3.5, 3, 6, 4 ), {oeeMachineId: 1} ), ...rows().slice( 1 ), row( 9, 'Zone', rect( 18, 9, 4, 2 ), {sort: 1} )] } );
		p.button( 'Save' ).click();
		await stable( p.fixture );
		expect( p.closed ).toEqual( [] );
		expect( p.service.floor ).toHaveBeenCalledWith( p.gateway, 10 );
		expect( p.said() ).toEqual( ['The gateway refused the save:  The floor is in use.'] );
		expect( p.el().querySelector( '.said' )!.getAttribute( 'role' ) ).toBe( 'alert' );
		expect( p.editor.edit().shapes.map( s=>[s.key, s.id] ) ).toEqual( [['s1', 1], ['s7', 7], ['s8', 8], ['n1', 9]] );
		expect( p.editor.batch() ).toEqual( [{ op: 'updateFloor', args: {height: 15} }] );//what is left to write
		await p.key( p.shape( 'Filler' ), 'ArrowRight' );
		expect( p.said() ).toEqual( [] );//an edit answers it
	} );
	it( 'holds the floor still while a save is on its way, and waits for Read again where the rows could not be read after a refusal', async ()=>{
		const p = await create();
		( <HTMLElement>p.offer( 'Zone' ) ).click();
		await stable( p.fixture );
		let refuse!:( e:Error )=>void;
		p.service.save.mockImplementationOnce( ()=>new Promise<void>( ( _, reject )=>{ refuse = reject; } ) );
		p.button( 'Save' ).click();
		await stable( p.fixture );
		const inert = ()=>['.sizes', '.tools', '.work'].map( selector=>p.el().querySelector( selector )!.hasAttribute( 'inert' ) );
		expect( inert() ).toEqual( [true, true, true] );
		p.service.floor.mockRejectedValueOnce( new Error( 'The socket closed.' ) );
		refuse( new Error( 'The floor is in use.' ) );
		await stable( p.fixture );
		expect( inert() ).toEqual( [false, false, false] );
		expect( p.said() ).toEqual( ["The gateway refused the save:  The floor is in use.  The floor couldn't be read again."] );
		await p.key( p.shape( 'Filler' ), 'ArrowRight' );
		expect( p.said() ).toEqual( ["The save was refused part-way, and the floor couldn't be read again:  Read again, and Save writes what is left."] );
		expect( p.greyed( p.button( 'Save' ) ) ).toBe( true );//what went in is unknown:  the zone may be made twice
		p.service.floor.mockRejectedValueOnce( new Error( 'The socket closed.' ) );
		p.button( 'Read again' ).click();
		await stable( p.fixture );
		expect( p.said() ).toEqual( ["The floor still couldn't be read."] );
		p.service.floor.mockResolvedValueOnce( { floor, shapes: [...rows(), row( 9, 'Zone', rect( 18, 9, 4, 2 ), {sort: 1} )] } );//the zone went in
		p.button( 'Read again' ).click();
		await stable( p.fixture );
		expect( p.button( 'Read again' ) ).toBeUndefined();
		expect( p.editor.batch() ).toEqual( [{ op: 'updateShape', id: 1, args: {points: rect( 3.5, 3, 6, 4 )} }] );
		expect( p.greyed( p.button( 'Save' ) ) ).toBe( false );
	} );
	it( 'exports the floor as json, and imports one onto an empty floor alone', async ()=>{
		const p = await create();
		const url = vi.spyOn( URL, 'createObjectURL' ).mockReturnValue( 'blob:x' );
		const click = vi.spyOn( HTMLAnchorElement.prototype, 'click' ).mockImplementation( ()=>{} );
		p.button( 'Export' ).click();
		expect( click.mock.contexts.map( a=>( <HTMLAnchorElement>a ).download ) ).toEqual( ['plant-floor.json'] );
		const text = await new Promise<string>( resolve=>{ const reader = new FileReader(); reader.onload = ()=>resolve( <string>reader.result ); reader.readAsText( <Blob>url.mock.calls[0][0] ); } );
		expect( text ).toBe( '{ "width": 40, "height": 20, "grid": 0.5, "shapes": [\n\t{ "kind": "Machine", "machine": "filler", "points": [[3,3],[9,3],[9,7],[3,7]] },\n\t{ "kind": "Zone", "label": "Conveyor", "points": [[9,4.5],[13,4.5],[13,5.5],[9,5.5]] },\n\t{ "kind": "Label", "label": "Dock", "points": [[38,15]] }\n]}' );
		expect( p.greyed( p.button( 'Import' ) ) ).toBe( true );//a floor with shapes
		const input = ( file:string )=><HTMLInputElement><unknown>{ files: [{ text: async ()=>file }], value: 'floor.json' };
		await p.editor.onFile( input( text ) );
		await stable( p.fixture );
		expect( p.said() ).toEqual( ['Import reads onto an empty floor, and this one has 3 shapes:  delete them first.'] );
		const empty = await ( TestBed.resetTestingModule(), create( {shapes: []} ) );//a second editor, on a floor with none
		expect( empty.greyed( empty.button( 'Import' ) ) ).toBe( false );
		await empty.editor.onFile( input( text.replace( '"width": 40', '"width": 60' ).replace( 'filler', 'mixer' ) ) );
		await stable( empty.fixture );
		expect( empty.said() ).toEqual( ['Shape 1:  machine "mixer" isn\'t one the floor\'s line reaches.'] );
		await empty.type( 1, '' );//a height typed that isn't one, the file's the floor's
		const read = input( text.replace( '"width": 40', '"width": 60' ) );
		await empty.editor.onFile( read );
		await stable( empty.fixture );
		expect( read.value ).toBe( '' );
		expect( empty.said() ).toEqual( [] );
		expect( empty.editor.edit().shapes.map( s=>[s.key, s.id, s.kind, s.machineId] ) ).toEqual( [['n1', undefined, 'Machine', 1], ['n2', undefined, 'Zone', undefined], ['n3', undefined, 'Label', undefined]] );
		expect( empty.editor.port.view().width ).toBe( 61 );//the floor read, fitted
		expect( [...empty.el().querySelectorAll<HTMLInputElement>( '.sizes input' )].map( i=>i.value ) ).toEqual( ['60', '20', '0.5'] );//the floor read, the typed text gone
		expect( empty.editor.batch().map( m=>m.op ) ).toEqual( ['updateFloor', 'createShape', 'createShape', 'createShape'] );
	} );
} );