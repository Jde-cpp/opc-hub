import { AfterViewInit, Component, computed, effect, ElementRef, inject, input, linkedSignal, OnDestroy, output, signal, untracked, viewChild, viewChildren } from '@angular/core';
import { MatButtonModule } from '@angular/material/button';
import { MatFormFieldModule } from '@angular/material/form-field';
import { MatIconModule } from '@angular/material/icon';
import { MatInputModule } from '@angular/material/input';
import { MatTooltipModule } from '@angular/material/tooltip';
import { errorMessage, saveFile } from 'jde-framework';
import { OeeBox, OeeFloorKind, OeeFloorRights, OeePoint, pointsBox } from '../../../model/oee-floor';
import { addShape, addVertex, EditShape, FloorBase, floorBatch, floorBatchQL, floorBatchRefusal, floorEdit, FloorEdit, floorEditProblem, floorExport, FloorGeometry, floorGeometry, FloorHandle, floorHandles, floorImport, floorReach, floorRebase, floorShapeTitle, floorSides, FloorSize, floorStranded, floorUnplaced, handlePoint, isRectangle, movePoints, moveVertex, NewShape, newShapePoints, removeVertex, resizePoints, rotatePoints, withoutShape, withShape } from '../../../model/oee-floor-edit';
import { OeeLineRef, OeePlant } from '../../../model/oee-lines';
import type { HistPoster, HistReader } from '../../../services/history-service';
import { OeeFloorService } from '../../../services/oee-floor-service';
import { FloorViewport } from './oee-floor-viewport';

//what the side lists offer:  a machine or a line the floor's line reaches that has no shape yet, or a fixture
type Offer = { id:string; label:string; icon:string; shape:NewShape };
//a shape as the editor draws it, in drawing order
type Drawn = { shape:EditShape; geometry:FloorGeometry; title:string; text?:string; box:OeeBox; stranded:boolean };
//a handle of the selected shape:  one of its box's eight, one of its points, or a side's middle, where a point is added
type Grip = { id:string; at:OeePoint; label:string; box?:FloorHandle; vertex?:number; side?:number };
//what the pointers are doing:  the view dragged or pinched, or the selected shape moved, resized by a handle or reshaped by a point
type Gesture = { kind:'view'|'move'|'box'|'vertex'; moved:number; background?:boolean; key?:string; at?:{x:number; y:number}; points?:OeePoint[]; handle?:FloorHandle; index?:number };
const layers:Record<OeeFloorKind,number> = { Zone: 0, Aisle: 1, Wall: 2, Line: 3, Machine: 4, Label: 5 };//bottom to top, as the map draws
const handleNames:Record<FloorHandle,string> = { nw: 'top left corner', n: 'top side', ne: 'top right corner', e: 'right side', se: 'bottom right corner', s: 'bottom side', sw: 'bottom left corner', w: 'left side' };
const arrows:Record<string,{dx:number; dy:number}> = { ArrowLeft: {dx: -1, dy: 0}, ArrowRight: {dx: 1, dy: 0}, ArrowUp: {dx: 0, dy: -1}, ArrowDown: {dx: 0, dy: 1} };

//The floor's editor (#482, docs/demo/spec.md *Editing*), in the page's place while Edit is on, in a chunk of its own the page
//imports then:  the floor's size and grid;  the machines and lines the floor's line reaches that have no shape yet and the four
//fixtures, each dragged onto the floor or pressed to stand at the middle of the view, a rectangle, a run or a point of a
//default size;  a shape selected by a press, moved by a drag, resized by its box's eight handles, turned a quarter, reshaped by
//its points - one dragged, one added at a side's middle, one taken out - and deleted, every point on the grid and on the
//floor, in metres:  the svg's viewBox is the floor's, so a pointer is read in metres at any zoom.  From the keyboard a shape
//moves by the arrows a grid at a time and resizes with Shift, turns by R and goes by Delete, and a handle moves by the arrows;
//a shape just placed has the focus, so the keys are its.
//Save writes what differs from the rows as read as one batch (floorBatch), greyed with the reason while the gateway's rules
//or the caller's rights would refuse it, and Cancel drops it;  a save the gateway refuses part-way is read back, so the next
//writes what is left.  Export saves the floor as the spec's json and Import reads one onto an empty floor.
@Component({
	selector: 'oee-floor-editor',
	templateUrl: './oee-floor-editor.html',
	styleUrl: './oee-floor-editor.scss',
	imports: [MatButtonModule, MatFormFieldModule, MatIconModule, MatInputModule, MatTooltipModule]
})
export class OeeFloorEditor implements AfterViewInit, OnDestroy{
	gateway = input.required<HistReader & HistPoster>();
	line = input.required<OeeLineRef>();
	source = input.required<FloorBase>();//the floor and its shapes as the page read them
	plant = input.required<OeePlant>();
	rights = input.required<OeeFloorRights>();
	closed = output<boolean>();//true once saved

	base = linkedSignal( ()=>this.source() );//the rows the batch is from:  the page's, or those read after a refused save
	edit = linkedSignal<FloorEdit>( ()=>floorEdit( this.source() ) );
	selected = signal<string|undefined>( undefined );
	mode = signal<'box'|'points'>( 'box' );//the selected shape's handles:  its box's, or its points
	busy = signal( false );
	ghost = signal<{geometry:FloorGeometry; points:OeePoint[]}|undefined>( undefined );//where what is dragged from a list would stand
	readonly port = new FloorViewport();
	constructor(){
		effect( ()=>{//another floor:  the whole of it
			const floor = this.source().floor;
			untracked( ()=>this.port.fit( floor ) );
		} );
		effect( ()=>{//a shape just placed is drawn:  the focus is its, so the arrows, R and Delete are
			const element = this.shapeElements().find( e=>e.nativeElement.dataset['key']==this.#placed );
			if( element ){
				this.#placed = undefined;
				element.nativeElement.focus();
			}
		} );
	}
	ngAfterViewInit():void{ this.#unobserve = this.port.observe( this.#svg() ); }
	ngOnDestroy():void{ this.#unobserve?.(); }

	readonly reach = computed( ()=>floorReach( this.plant(), this.line().id ) );
	readonly drawn = computed<Drawn[]>( ()=>{
		const plant = this.plant(), reach = this.reach();
		return this.edit().shapes.map( ( shape, i )=>({ shape, i }) ).sort( ( a, b )=>layers[a.shape.kind]-layers[b.shape.kind] || a.shape.sort-b.shape.sort || a.i-b.i ).map( ( {shape} ):Drawn=>{
			const title = floorShapeTitle( shape, plant ), named = shape.kind=='Machine' || shape.kind=='Line';
			return { shape, geometry: floorGeometry( shape.kind ), title, text: named ? title : shape.label, box: pointsBox( shape.points ), stranded: floorStranded( shape, reach ) };
		} );
	} );
	readonly shape = computed( ()=>this.edit().shapes.find( s=>s.key==this.selected() ) );
	//a machine's or a line's own name, which its label overrides on the map
	readonly ownName = computed( ()=>{ const s = this.shape(); return s && ( s.kind=='Machine' || s.kind=='Line' ) ? floorShapeTitle( {...s, label: undefined}, this.plant() ) : undefined; } );
	readonly boxed = computed( ()=>{ const s = this.shape(), b = s ? pointsBox( s.points ) : undefined; return !!b && b.width>0 && b.height>0; } );//a box with room to resize
	//its handles are its points:  asked for, or a run with no box to resize.  A label is one point, and has none.
	readonly byPoints = computed( ()=>{ const s = this.shape(); return !!s && s.kind!='Label' && ( this.mode()=='points' || !this.boxed() ); } );
	readonly grips = computed<Grip[]>( ()=>{
		const s = this.shape();
		if( !s || s.kind=='Label' )
			return [];
		if( !this.byPoints() )
			return floorHandles.map( ( h ):Grip=>({ id: h, at: handlePoint( s.points, h ), label: `Resize by the ${handleNames[h]}`, box: h }) );
		return [
			...s.points.map( ( at, i ):Grip=>({ id: `v${i}`, at, label: `Point ${i+1} of ${s.points.length}`, vertex: i }) ),
			...floorSides( s.points, s.kind ).map( ( side ):Grip=>({ id: `a${side.index}`, at: side.at, label: `Add a point after point ${side.index+1}`, side: side.index }) )
		];
	} );
	points( p:OeePoint[] ):string{ return p.map( ( [x, y] )=>`${x},${y}` ).join( ' ' ); }

	//The side lists:  what isn't placed, and the four fixtures.
	readonly offers = computed<Offer[]>( ()=>{
		const unplaced = floorUnplaced( this.reach(), this.edit().shapes );
		return [
			...unplaced.machines.map( ( m ):Offer=>({ id: `m${m.id}`, label: m.name, icon: 'precision_manufacturing', shape: {kind: 'Machine', machineId: m.id} }) ),
			...unplaced.lines.map( ( l ):Offer=>({ id: `l${l.id}`, label: l.level ? `${l.name}, ${l.level}` : l.name, icon: 'linear_scale', shape: {kind: 'Line', lineId: l.id} }) )
		];
	} );
	readonly fixtures:Offer[] = [
		{ id: 'Zone', label: 'Zone', icon: 'crop_square', shape: {kind: 'Zone'} }, { id: 'Wall', label: 'Wall', icon: 'horizontal_rule', shape: {kind: 'Wall'} },
		{ id: 'Aisle', label: 'Aisle', icon: 'more_horiz', shape: {kind: 'Aisle'} }, { id: 'Label', label: 'Label', icon: 'title', shape: {kind: 'Label', label: 'Label'} }
	];
	//An offer pressed stands at the middle of the view:  by a pointer that stayed put, at its release, and from the keyboard or
	//assistive technology by their click, which has no pointer behind it (detail 0) - a pointer's own click comes after its
	//release, which has placed already.  Dragged, it stands where it is let go over the floor, its outline following the pointer
	//there, and nowhere when let go beside it.
	pick( e:MouseEvent, offer:Offer ):void{
		if( e.detail==0 )
			this.#add( offer, this.#middle() );
	}
	lift( e:PointerEvent, offer:Offer ):void{
		if( e.button!=0 && e.pointerType=='mouse' )
			return;
		this.#carry = { offer, id: e.pointerId, x: e.clientX, y: e.clientY, moved: false };
		( <Element>e.currentTarget ).setPointerCapture?.( e.pointerId );
	}
	carry( e:PointerEvent ):void{
		const c = this.#carry;
		if( c?.id!=e.pointerId )
			return;
		c.moved ||= Math.abs( e.clientX-c.x )+Math.abs( e.clientY-c.y )>=OeeFloorEditor.slop;
		const at = c.moved ? this.#over( e ) : undefined;
		this.ghost.set( at ? { geometry: floorGeometry( c.offer.shape.kind ), points: newShapePoints( c.offer.shape.kind, at, this.edit().floor ) } : undefined );
	}
	drop( e:PointerEvent, cancelled=false ):void{
		const c = this.#carry;
		if( c?.id!=e.pointerId )
			return;
		this.#carry = undefined;
		this.ghost.set( undefined );
		const at = cancelled ? undefined : c.moved ? this.#over( e ) : this.#middle();
		if( at )
			this.#add( c.offer, at );
	}
	#middle():OeePoint{ const c = this.port.centre(); return [c.x, c.y]; }
	#add( offer:Offer, at:OeePoint ):void{
		const added = addShape( this.edit(), offer.shape, at );
		this.edit.set( added.edit );
		this.select( added.key );
		this.#placed = added.key;
	}
	//the point in metres under a pointer that is over the svg
	#over( e:{clientX:number; clientY:number} ):OeePoint|undefined{
		const svg = this.#svg(), r = svg?.getBoundingClientRect();
		if( !r || e.clientX<r.left || e.clientX>r.right || e.clientY<r.top || e.clientY>r.bottom )
			return undefined;
		const at = this.port.metres( e, svg );
		return [at.x, at.y];
	}

	//A shape is selected by a press or Enter, its handles its box's where it is a rectangle and its points where it isn't;  the
	//toolbar's switch shows the other.
	select( key:string ):void{
		if( key==this.selected() )
			return;
		this.selected.set( key );
		const s = this.shape();
		this.mode.set( s && isRectangle( s.points ) ? 'box' : 'points' );
	}
	rotate():void{ this.#change( ( s, floor )=>({ points: rotatePoints( s.points, floor ) }) ); }
	setLabel( text:string ):void{ this.#change( ()=>({ label: text.trim() || undefined }) ); }
	remove( key=this.selected() ):void{
		if( !key )
			return;
		this.edit.update( edit=>withoutShape( edit, key ) );
		if( this.selected()==key )
			this.selected.set( undefined );
		this.#svg()?.focus();//the shape's element is gone, and the focus with it
	}
	#change( change:( s:EditShape, floor:FloorSize )=>Partial<EditShape> ):void{
		const s = this.shape();
		if( s )
			this.edit.update( edit=>withShape( edit, s.key, change( s, edit.floor ) ) );
	}
	#points( key:string, points:OeePoint[] ):void{ this.edit.update( edit=>withShape( edit, key, {points} ) ); }

	//Pointers on the svg, which takes each at its press, so a drag goes on past its edge:  on a shape it selects and moves it, on
	//a handle resizes or reshapes it, on the floor pans, and a press there that stays put selects nothing.  A second finger makes
	//it a pinch, and what the first was moving goes back.  Nothing moves until the pointer has, by a few pixels.
	press( e:PointerEvent, key?:string ):void{
		if( e.button!=0 && e.pointerType=='mouse' )
			return;
		e.stopPropagation();
		if( key )
			this.select( key );
		const s = key ? this.shape() : undefined;
		this.#begin( e, s ? { kind: 'move', key: s.key, points: s.points } : { kind: 'view', background: true } );
	}
	grab( e:PointerEvent, grip:Grip ):void{
		const s = this.shape();
		if( !s || ( e.button!=0 && e.pointerType=='mouse' ) )
			return;
		e.stopPropagation();
		if( grip.box )
			return this.#begin( e, { kind: 'box', key: s.key, points: s.points, handle: grip.box } );
		const points = grip.side==undefined ? s.points : addVertex( s.points, grip.side );//a side's middle is a point from its press
		if( grip.side!=undefined )
			this.#points( s.key, points );
		this.#begin( e, { kind: 'vertex', key: s.key, points, index: grip.vertex ?? grip.side!+1 } );
	}
	#begin( e:PointerEvent, gesture:Omit<Gesture,'moved'|'at'> ):void{
		const svg = this.#svg();
		this.#pointers.set( e.pointerId, {x: e.clientX, y: e.clientY} );
		svg?.setPointerCapture?.( e.pointerId );
		const first = this.#gesture;
		if( this.#pointers.size>1 ){
			if( first?.key && first.points )
				this.#points( first.key, first.points );
			this.#gesture = { kind: 'view', moved: OeeFloorEditor.slop };
		}
		else
			this.#gesture = { ...gesture, moved: 0, at: this.port.metres( e, svg ) };
	}
	move( e:PointerEvent ):void{
		const was = this.#pointers.get( e.pointerId ), g = this.#gesture;
		if( !was || !g )
			return;
		const now = { x: e.clientX, y: e.clientY }, floor = this.edit().floor, svg = this.#svg();
		if( g.kind=='view' )
			g.moved += this.port.drag( this.#pointers, e.pointerId, now, floor, svg );
		else{
			g.moved += Math.abs( now.x-was.x )+Math.abs( now.y-was.y );
			if( g.moved>=OeeFloorEditor.slop ){
				const to = this.port.metres( e, svg ), at:OeePoint = [to.x, to.y];
				this.#points( g.key!, g.kind=='move' ? movePoints( g.points!, to.x-g.at!.x, to.y-g.at!.y, floor ) : g.kind=='box' ? resizePoints( g.points!, g.handle!, at, floor ) : moveVertex( g.points!, g.index!, at, floor ) );
			}
		}
		this.#pointers.set( e.pointerId, now );
	}
	release( e:PointerEvent ):void{
		const g = this.#gesture;
		if( !this.#pointers.delete( e.pointerId ) )
			return;
		if( g?.background && g.moved<OeeFloorEditor.slop )
			this.selected.set( undefined );
		if( !this.#pointers.size )
			this.#gesture = undefined;
	}
	wheel( e:WheelEvent ):void{ this.port.wheel( e, this.edit().floor, this.#svg() ); }
	static readonly slop = 6;//px of movement past which a press is a drag

	//The keyboard, on a shape:  Enter selects it, the arrows move it a grid at a time, and with Shift take its right and bottom
	//sides, R turns it, Delete deletes it, Escape lets it go.  On a handle the arrows move it, Enter adds a side's point and
	//Delete takes a point out.
	key( e:KeyboardEvent, key:string ):void{
		const s = this.edit().shapes.find( x=>x.key==key );
		if( s && e.target==e.currentTarget && this.#shapeKey( e, s ) )
			e.preventDefault();
	}
	#shapeKey( e:KeyboardEvent, s:EditShape ):boolean{
		const floor = this.edit().floor, step = arrows[e.key];
		if( e.key=='Enter' || e.key==' ' )
			this.select( s.key );
		else if( step ){
			this.select( s.key );
			const b = pointsBox( s.points );
			this.#points( s.key, e.shiftKey ? resizePoints( s.points, step.dx ? 'e' : 's', [b.x+b.width+step.dx*floor.grid, b.y+b.height+step.dy*floor.grid], floor ) : movePoints( s.points, step.dx*floor.grid, step.dy*floor.grid, floor ) );
		}
		else if( e.key=='Delete' || e.key=='Backspace' )
			this.remove( s.key );
		else if( e.key=='r' || e.key=='R' ){
			this.select( s.key );
			this.rotate();
		}
		else if( e.key=='Escape' )
			this.selected.set( undefined );
		else
			return false;
		return true;
	}
	//a box's handle takes the arrows, and leaves the shape's other keys the shape's
	gripKey( e:KeyboardEvent, grip:Grip ):void{
		const s = this.shape(), floor = this.edit().floor, step = arrows[e.key];
		if( !s )
			return;
		if( step && grip.side==undefined ){
			const to:OeePoint = [grip.at[0]+step.dx*floor.grid, grip.at[1]+step.dy*floor.grid];
			this.#points( s.key, grip.box ? resizePoints( s.points, grip.box, to, floor ) : moveVertex( s.points, grip.vertex!, to, floor ) );
		}
		else if( grip.side!=undefined && ( e.key=='Enter' || e.key==' ' ) )
			this.#points( s.key, addVertex( s.points, grip.side ) );
		else if( grip.vertex!=undefined && ( e.key=='Delete' || e.key=='Backspace' ) )
			this.#points( s.key, removeVertex( s.points, grip.vertex, s.kind ) );
		else if( !grip.box || !this.#shapeKey( e, s ) )
			return;
		e.preventDefault();
	}

	//The floor's size and grid, each applied once it is a number above zero;  until then Save says which isn't.
	setSize( field:keyof FloorSize, text:string ):void{
		const value = Number( text ), valid = !!text.trim() && isFinite( value ) && value>0;
		this.#typed.update( typed=>({ ...typed, [field]: valid ? undefined : text }) );
		if( valid ){
			this.edit.update( edit=>({ ...edit, floor: {...edit.floor, [field]: value} }) );
			if( field!='grid' )//another size:  the whole of it
				this.port.fit( this.edit().floor );
		}
	}
	#typed = signal<Partial<Record<keyof FloorSize,string>>>( {} );
	readonly sizeProblem = computed( ()=>{
		const typed = this.#typed(), field = ( <const>['width', 'height', 'grid'] ).find( f=>typed[f]!=undefined );
		return field ? `The floor's ${field} must be a number above zero.` : undefined;
	} );

	//Save:  the batch from the rows as read to the floor as edited, and why it is greyed - what the gateway's rules would refuse
	//of the floor, what the batch needs of the caller's rights, or nothing changed.
	readonly batch = computed( ()=>floorBatch( this.base(), this.edit() ) );
	readonly problem = computed( ()=>this.sizeProblem() ?? floorEditProblem( this.edit(), s=>floorShapeTitle( s, this.plant() ) ) );
	readonly refusal = computed( ()=>floorBatchRefusal( this.batch(), this.rights() ) );
	readonly saveReason = computed( ()=>this.problem() ?? this.refusal() ?? ( this.batch().length ? undefined : 'Nothing is changed.' ) );
	readonly canSave = computed( ()=>!this.busy() && !this.saveReason() );
	//what the gateway said to the last save, or an import's problem, while the floor is as it was then:  an edit answers it
	#said = signal<{of:FloorEdit; text:string}|undefined>( undefined );
	readonly error = computed( ()=>this.#said()?.of===this.edit() ? this.#said()!.text : undefined );
	async save():Promise<void>{
		if( !this.canSave() )
			return;
		const gateway = this.gateway(), was = this.base();
		this.busy.set( true );
		try{
			await this.#service.save( gateway, floorBatchQL( was.floor.id, this.batch() ) );
			this.closed.emit( true );
		}
		catch( e ){
			//the gateway ended the batch at what it refused, with what came before it written:  the rows as they are now, under the floor as edited
			const now = await this.#service.floor( gateway, this.line().id ).catch( ()=>undefined );
			if( now ){
				this.edit.update( edit=>floorRebase( edit, was, now ) );
				this.base.set( now );
			}
			this.#said.set( {of: this.edit(), text: `The gateway refused the save:  ${errorMessage( e )}`} );
		}
		finally{
			this.busy.set( false );
		}
	}
	export():void{ saveFile( `${this.line().slug}-floor.json`, floorExport( this.edit(), this.reach() ) ); }
	async onFile( input:HTMLInputElement ):Promise<void>{
		const file = input.files?.[0];
		if( !file )
			return;
		const read = floorImport( await file.text(), this.edit(), this.reach() );
		input.value = '';//the same file again is a change
		if( 'problem' in read )
			return this.#said.set( {of: this.edit(), text: read.problem} );
		this.edit.set( read.edit );
		this.#typed.set( {} );
		this.selected.set( undefined );
		this.port.fit( read.edit.floor );
	}

	svg = viewChild<ElementRef<SVGSVGElement>>( 'svg' );
	shapeElements = viewChildren<ElementRef<SVGGElement>>( 'shapeElement' );
	#placed?:string;//the shape just placed, until it is drawn and has the focus
	#svg():SVGSVGElement|undefined{ return this.svg()?.nativeElement; }
	#pointers = new Map<number,{x:number; y:number}>();
	#gesture?:Gesture;
	#carry?:{offer:Offer; id:number; x:number; y:number; moved:boolean};
	#unobserve?:()=>void;
	#service = inject( OeeFloorService );
}