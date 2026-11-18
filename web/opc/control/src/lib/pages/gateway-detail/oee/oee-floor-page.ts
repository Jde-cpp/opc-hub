import { DatePipe, Location } from '@angular/common';
import { Component, computed, effect, ElementRef, inject, model, OnDestroy, OnInit, Signal, signal, untracked, viewChild, ViewContainerRef, WritableSignal } from '@angular/core';
import { ActivatedRoute, RouterLink } from '@angular/router';
import { MatButtonModule } from '@angular/material/button';
import { MatIconModule } from '@angular/material/icon';
import { MatTooltipModule } from '@angular/material/tooltip';
import { Subscription } from 'rxjs';
import { ComponentPageTitle, RouteItem } from 'jde-spa';
import { errorMessage, SnackbarService } from 'jde-framework';
import { oeeDuration, oeePercent, OeeSplitRow } from '../../../model/oee';
import { FloorDrawing, floorDrawing, floorLineKey, floorListOrder, OeeFloorLegendItem, oeeFloorLegend, oeeFloorLineLegend, OeeFloorLink, OeeFloorRights, OeeFloorState, oeeFloorState, floorShapeName, floorStateText, oeeLiveSplit, shiftLeft, zoneClock } from '../../../model/oee-floor';
import { ExNodeId } from '../../../model/ex-node-id';
import { parseHistNodeText } from '../../../model/hist-admin';
import { OeeLiveEvent, OeeLiveStatus } from '../../../model/oee-live';
import { OeeMachineRef, oeeTileFigures, oeeTileNotes } from '../../../model/oee-live-view';
import { OeeLineRef } from '../../../model/oee-lines';
import { NodePlace, pageSegments } from '../../../model/node';
import { Gateway, gatewayAnswer, namespacesOf, placeNode } from '../../../services/gateway-service';
import { OeeFloorService } from '../../../services/oee-floor-service';
import { OeeFloorPageData } from '../../../services/resolvers/oee-floor-resolver';
import { FloorMapShape, FloorShapeView, OeeFloorMap } from './oee-floor-map';

//A placed machine or line, live:  the status its subscription last brought, how the subscription stands and why it ended, each a
//signal of its own, so a push redraws its own shape alone;  its state is computed from them and the page's clock.
type LiveShape = {
	key:string; kind:'machine'|'line'; name:string; slug:string; machine?:OeeMachineRef; line?:OeeLineRef;
	status:WritableSignal<OeeLiveStatus|undefined>; link:WritableSignal<OeeFloorLink>; ended:WritableSignal<string|undefined>;
	state:Signal<OeeFloorState>; subscription?:Subscription;
};
//the side panel's links for a machine:  its group's page, and its group's root node in the browser, whose History tab is the trend
type PanelLinks = { key:string; group?:string[]; node?:string[] };
//a row of the list view:  the dashboard's tile for the machine or the line
type ListTile = { key:string; kind:'machine'|'line'; name:string; level?:string; link:string[]; state:OeeFloorState; figures:{label:string; value:string}[]; period?:string; partial:boolean; notes:string[] };

//A line's floor page (#481, docs/demo/spec.md *Drawing*, *Live*), `lines/<slug>/floor` under its gateway's page:  the header
//with the line, its shift and the time left in it from the line's own status, the plant's clock in the line's calendar's zone,
//the line's figures and the legend;  the map, an SVG of the floor in metres with each placed machine outlined by its state and
//each line it reaches boxed or drawn, live over one OeeSubscribe per Machine shape, one per Line shape and boxed line, and one
//for the floor's own line;  the side panel for the shape under the pointer, the focus or a tap, with its figures, the loss split,
//the stop in progress, the last count and its links;  and below 600 pixels of width the list view, the dashboard's tiles in the
//floor's order.  A subscription refused draws its shape No access, one ended otherwise Ended, and the others stand.  The "since"
//text and the clock tick from a one-second timer, never a read.  The resolver read the floor, its shapes and the plant, and the
//router reuses the page from one line's floor to the next.  Edit (#482, spec *Editing*), for a caller the gateway says may
//update the floor's shapes, puts the editor in the map's place, imported then, until its Save or Cancel;  a save reads the floor
//again and draws it.
@Component({
	templateUrl: './oee-floor-page.html',
	styleUrls: ['./oee-floor-page.scss', './oee-tiles.scss'],
	host: {class: 'main-content my-content oee-floor-page', '(keydown.escape)': 'pinned.set( undefined )'},
	imports: [DatePipe, RouterLink, MatButtonModule, MatIconModule, MatTooltipModule, OeeFloorMap]
})
export class OeeFloorPage implements OnInit, OnDestroy{
	ngOnInit(){
		this.#route.data.subscribe( data=>this.#open( <OeeFloorPageData>data['pageData'] ) );
		this.#clock = setInterval( ()=>this.now.set( new Date() ), 1000 );
	}
	ngOnDestroy(){
		this.#close();
		clearInterval( this.#clock );
	}
	#open( pageData:OeeFloorPageData ):void{
		if( !pageData )//the resolver found no floor, and is on its way elsewhere
			return;
		this.#close();
		const gateway = pageData.gateway, line = pageData.line;
		if( this.gateway!==gateway )//the rights are the gateway's tables', asked once for it
			this.#readRights( gateway );
		this.gateway = gateway;
		this.sideNav.set( pageData.routing );
		this.#title.title = `${line.name} - Floor`;
		const drawing = floorDrawing( pageData.floor, pageData.shapes, pageData.plant, line.id );
		const shapes = [
			...drawing.lines.map( l=>this.#shape( l.key, 'line', l.name, {line: l.line} ) ),
			...drawing.machines.map( m=>this.#shape( m.key, 'machine', m.name, {machine: m.machine} ) )
		];
		this.#shapes = new Map( shapes.map( s=>[s.key, s] ) );
		const own = this.#shape( floorLineKey( line.id ), 'line', line.name, {line} );
		this.own.set( own );
		this.data.set( pageData );
		this.drawing.set( drawing );
		this.mapShapes.set( [...drawing.lines, ...drawing.machines].map( ( d ):FloorMapShape=>{
			const live = this.#shapes.get( d.key )!;
			return { key: d.key, kind: live.kind, name: d.name, points: d.points, box: d.box, view: this.#view( live ) };
		} ) );
		for( const s of [...shapes, own] )
			s.subscription = ( s.kind=='machine' ? gateway.oeeSubscribe( s.machine!.id ) : gateway.oeeSubscribeLine( s.line!.id ) ).subscribe( {next: e=>this.#on( s, e ), error: e=>this.#ended( s, e )} );
	}
	#shape( key:string, kind:'machine'|'line', name:string, ref:{machine?:OeeMachineRef; line?:OeeLineRef} ):LiveShape{
		const status = signal<OeeLiveStatus|undefined>( undefined ), link = signal<OeeFloorLink>( 'connecting' );
		return { key, kind, name, slug: ref.machine?.slug ?? ref.line?.slug ?? '', ...ref, status, link, ended: signal( undefined ), state: computed( ()=>oeeFloorState( status(), link(), this.now() ) ) };
	}
	#view( live:LiveShape ):Signal<FloorShapeView>{
		return computed( ()=>{
			const s = live.status(), f = s?.figures, state = live.state(), c = s?.machines;
			return {
				state, oee: oeePercent( f?.oee ), availability: oeePercent( f?.availability ), performance: oeePercent( f?.performance ), quality: oeePercent( f?.quality ),
				...(c ? {count: `${c.running} of ${c.running+c.stopped+c.unknown} running`} : {}), aria: floorShapeName( live.name, state, f )
			};
		} );
	}
	#on( s:LiveShape, event:OeeLiveEvent ):void{
		if( event.kind=='dropped' )
			s.link.set( 'dropped' );
		else{
			s.status.set( event.status );
			s.link.set( 'live' );
		}
	}
	//The gateway ended the subscription, or refused it:  a 403 is a group the user may not read, No access;  a 401 the session the
	//gateway doesn't know;  any other end says why in the panel.  The other shapes stand.
	#ended( s:LiveShape, e:unknown ):void{
		const answer = gatewayAnswer( e );
		if( answer?.httpStatus==403 )
			return s.link.set( 'refused' );
		s.ended.set( answer?.httpStatus==401 ? "The gateway doesn't know your session any more:  sign in again to see it." : answer?.message || errorMessage( e ) );
		s.link.set( 'ended' );
	}
	//the subscriptions end with the page, and with the floor it showed
	#close():void{
		for( const s of this.#live() )
			s.subscription?.unsubscribe();
		this.#shapes.clear();
		this.own.set( undefined );
		this.#links.clear();
		this.links.set( undefined );
		this.pinned.set( undefined );
		this.hovered.set( undefined );
		this.mapShapes.set( [] );
		this.drawing.set( undefined );
		this.data.set( undefined );
		this.editing.set( false );
	}

	//Edit:  shown where the gateway says the caller may update the floor's shapes, which is what its own gate would say to the
	//mutation;  a gateway that doesn't answer shows none.  A floor just made, from the line's OEE page, opens in its editor.
	rights = signal<OeeFloorRights|undefined>( undefined );
	editing = signal( false );
	readonly canEdit = computed( ()=>this.rights()?.updateShape===true );
	editorSlot = viewChild( 'editorSlot', {read: ViewContainerRef} );
	editButton = viewChild( 'editButton', {read: ElementRef<HTMLElement>} );
	async #readRights( gateway:Gateway ):Promise<void>{
		this.rights.set( undefined );
		const made = this.#made;
		this.#made = false;
		try{
			const rights = await this.#service.rights( gateway );
			if( this.gateway!==gateway )
				return;
			this.rights.set( rights );
			if( made && rights.updateShape && this.data() )
				this.editing.set( true );
		}
		catch( e ){
			console.warn( "Could not read the rights on the floor's tables:  the page offers no Edit.", e );
		}
	}
	//the editor, in a chunk of its own, over the floor as it was read;  dropped where Edit went off, or the floor changed, while it loaded
	async #mount( slot:ViewContainerRef ):Promise<void>{
		const d = this.data(), rights = this.rights();
		try{
			const { OeeFloorEditor } = await import( './oee-floor-editor' );
			if( !d || !rights || this.editorSlot()!==slot || this.data()!==d )
				return;
			const editor = slot.createComponent( OeeFloorEditor );
			editor.setInput( 'gateway', d.gateway );
			editor.setInput( 'line', d.line );
			editor.setInput( 'source', {floor: d.floor, shapes: d.shapes} );
			editor.setInput( 'plant', d.plant );
			editor.setInput( 'rights', rights );
			editor.instance.closed.subscribe( saved=>this.#edited( d, saved ) );
		}
		catch( e ){
			this.editing.set( false );
			this.#snackbar.exception( 'Could not load the editor', e );
		}
	}
	//The editor's Save or Cancel:  the map again, the focus back on Edit, and after a save the floor, its shapes and the plant
	//read again and drawn.
	async #edited( d:OeeFloorPageData, saved:boolean ):Promise<void>{
		if( this.data()!==d )
			return;
		this.#refocus = true;
		this.editing.set( false );
		if( !saved )
			return;
		try{
			const [now, plant] = await Promise.all( [this.#service.floor( d.gateway, d.line.id ), this.#service.plant( d.gateway )] );
			if( now && this.data()===d )
				this.#open( {...d, ...now, plant} );
		}
		catch( e ){
			this.#snackbar.exception( 'The floor is saved, but could not be read again', e );
		}
	}

	//The header:  the line, its shift and the time left from its own status - no calendar where the line names none, between
	//shifts where its calendar has none now - the plant's clock in its zone, its figures and its state.
	now = signal( new Date() );
	readonly clock = computed( ()=>zoneClock( this.now(), this.data()?.zone ?? 'UTC' ) );
	readonly shift = computed( ()=>{
		const d = this.data(), own = this.own(), s = own?.status(), now = this.now();
		if( !d )
			return '';
		if( s?.shift )
			return `${s.shift.name ?? 'Shift'}, ${shiftLeft( s.shift.end, now )}`;
		if( d.line.calendarId==undefined )
			return 'No calendar';
		if( !s )
			return own?.state().label ?? '';
		return s.calendarMissing ? 'Calendar not found' : 'Between shifts';
	} );
	readonly ownFigures = computed( ()=>oeeTileFigures( this.own()?.status()?.figures ) );
	readonly ownState = computed( ()=>this.own()?.state() );
	//the socket to the gateway dropped:  the map shows the last it said, until each subscription's next status
	readonly dropped = computed( ()=>{
		this.data();//another floor, other shapes
		return this.#live().some( s=>s.link()=='dropped' );
	} );
	readonly legend:OeeFloorLegendItem[] = [...oeeFloorLegend, ...oeeFloorLineLegend];

	//The map, and the shape it has under the pointer or the focus, or pinned by a tap or Enter:  the pinned one holds the panel
	//until it is tapped again, Escape, or another is pinned
	data = signal<OeeFloorPageData|undefined>( undefined );
	drawing = signal<FloorDrawing|undefined>( undefined );
	mapShapes = signal<FloorMapShape[]>( [] );
	hovered = signal<string|undefined>( undefined );
	pinned = signal<string|undefined>( undefined );
	readonly selected = computed( ()=>this.pinned() ?? this.hovered() );
	togglePin( key:string ):void{ this.pinned.update( p=>p==key ? undefined : key ); }

	//The side panel, standing beside the map whether a shape is selected or not, so it never takes the map's room:  the shape's
	//state, the shift's figures with the loss split, the dashboard tile's period and notes, the stop in progress, the last count
	//and its links - a machine's OEE page, its group's page and its root node in the browser, a line's its OEE page.  Its live
	//region says the shape and its state in words that hold still, as the "since" ticks each second.
	readonly panel = computed( ()=>{
		const key = this.selected(), s = key ? this.#shapes.get( key ) : undefined;
		if( !s )
			return undefined;
		const status = s.status(), f = status?.figures, stop = status?.state=='stopped' ? status.stop : undefined;
		const told = status ? oeeTileNotes( status, s.link()=='dropped' ) : { notes: [] };
		return {
			key, kind: s.kind, name: s.name, level: s.line?.level, state: s.state(), ended: s.ended(), link: s.link(),
			figures: oeeTileFigures( f ), split: f ? oeeLiveSplit( f ) : undefined, period: told.period, notes: told.notes, partial: f?.partial===true,
			stop, lastCount: status?.lastCount,
			oeeLink: s.kind=='machine' ? ['../../../machines', s.slug, 'oee'] : ['../../../lines', s.slug, 'oee']
		};
	} );
	readonly live = computed( ()=>{ const p = this.panel(); return p ? `${p.name}, ${floorStateText( p.state )}` : ''; } );
	links = signal<PanelLinks|undefined>( undefined );
	constructor(){
		effect( ()=>{//another shape in the panel:  a machine's links, read once
			const key = this.selected(), s = key ? this.#shapes.get( key ) : undefined;
			untracked( ()=>this.#describe( s ) );
		} );
		effect( ()=>{//Edit is on, and its slot is there
			const slot = this.editorSlot();
			if( slot )
				untracked( ()=>this.#mount( slot ) );
		} );
		effect( ()=>{//the editor closed, and Edit is there again:  the focus goes back to it
			const button = this.editButton();
			if( button && this.#refocus ){
				this.#refocus = false;
				button.nativeElement.focus();
			}
		} );
	}
	//The machine's links, read once:  a read in flight is shared by a shape selected again, and one for a floor since closed is
	//dropped.
	#describe( s:LiveShape|undefined ):void{
		if( !s?.machine )
			return this.links.set( undefined );
		const key = s.key;
		let read = this.#links.get( key );
		if( !read )
			this.#links.set( key, read = this.#read( s ) );
		this.links.set( {key} );
		read.then( links=>{
			if( this.#links.get( key )===read && this.selected()==key )
				this.links.set( links );
		} );
	}
	//the machine's group, for the panel's Group and Node links:  the node is the group's root placed on its connection, the
	//connection's own page for a group rooted at the Objects folder, and none for a root the gateway can't place
	async #read( s:LiveShape ):Promise<PanelLinks>{
		const key = s.key, gateway = this.gateway, links:PanelLinks = { key }, groupId = s.machine!.groupId;
		if( groupId!=undefined ){
			try{
				const group = await this.#service.group( gateway, groupId );
				if( group ){
					links.group = ['../../../groups', group.slug];
					if( group.connection ){
						const root = group.root ? parseHistNodeText( group.root ) : undefined;
						if( !root )
							links.node = ['/gateways', gateway.slug, group.connection];
						else{
							const place = await this.#place( gateway, group.connection, root );
							if( place?.path!=null )
								links.node = ['/gateways', gateway.slug, group.connection, ...pageSegments( place.path, place.nodeClass )];
						}
					}
				}
			}
			catch( e ){
				console.warn( `Could not describe the group of '${s.name}':  the panel has no link to it.`, e );
			}
		}
		return links;
	}
	//The group's root placed in the browser.  The gateway's `node` takes an id by the server's index, so a root spelled by its
	//namespace's URI, as a seeded group's is, goes through the connection's namespaces first (ExNodeId.byIndex):  asked by its URI
	//it answered no path, and the panel had no Node link.  None for a URI the server lacks.
	async #place( gateway:Gateway, connection:string, root:ExNodeId ):Promise<NodePlace|undefined>{
		const id = root.byIndex( root.nsu ? await namespacesOf(gateway, connection) : [] );
		return id ? placeNode( gateway, connection, id ) : undefined;
	}
	duration( ms:number ):string{ return oeeDuration( ms ); }
	share( row:OeeSplitRow ):string{ return row.share==undefined ? '' : oeePercent( row.share ); }

	//the list view:  a tile per line and machine in the floor's order, its state as the map says it
	readonly list = computed<ListTile[]>( ()=>{
		const d = this.drawing();
		return d ? floorListOrder( d ).map( item=>{
			const s = this.#shapes.get( item.key )!, status = s.status(), f = status?.figures, ended = s.ended();
			const told = status ? oeeTileNotes( status, s.link()=='dropped' ) : { notes: [] };
			return {
				key: s.key, kind: s.kind, name: s.name, level: s.line?.level, link: s.kind=='machine' ? ['../../../machines', s.slug, 'oee'] : ['../../../lines', s.slug, 'oee'],
				state: s.state(), figures: oeeTileFigures( f ), period: told.period, partial: f?.partial===true, notes: ended ? [...told.notes, ended] : told.notes
			};
		} ) : [];
	} );

	sideNav = model.required<RouteItem>();
	gateway!:Gateway;
	own = signal<LiveShape|undefined>( undefined );//the floor's line
	//the placed shapes and the floor's own line
	#live():LiveShape[]{ const own = this.own(); return [...this.#shapes.values(), ...(own ? [own] : [])]; }
	#shapes = new Map<string,LiveShape>();
	#links = new Map<string,Promise<PanelLinks>>();
	#clock:ReturnType<typeof setInterval>|undefined;
	#made = takeMade( inject( Location ) );//navigated here from a floor's making
	#route = inject( ActivatedRoute );
	#title = inject( ComponentPageTitle );
	#service = inject( OeeFloorService );
	#snackbar = inject( SnackbarService );
	#refocus = false;
}
//A floor just made opens in its editor once (#482):  the navigation's state says so, and the flag is taken from the history
//entry, so a Back or a reload to the page opens the map.
function takeMade( location:Location ):boolean{
	const state = <{edit?:boolean}|null>location.getState();
	if( state?.edit!==true )
		return false;
	const { edit, ...rest } = state;
	location.replaceState( location.path( true ), '', rest );
	return true;
}