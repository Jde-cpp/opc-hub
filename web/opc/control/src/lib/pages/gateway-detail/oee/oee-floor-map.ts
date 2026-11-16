import { AfterViewInit, Component, computed, effect, ElementRef, input, OnDestroy, output, Signal, signal, untracked, viewChild } from '@angular/core';
import { MatButtonModule } from '@angular/material/button';
import { MatIconModule } from '@angular/material/icon';
import { MatTooltipModule } from '@angular/material/tooltip';
import { FloorDrawing, floorTextLevel, OeeBox, OeeFloor, OeeFloorState, OeePoint } from '../../../model/oee-floor';

//A placed machine or line as the map draws it (#481, docs/demo/spec.md *Drawing*):  its state, its figures as text, a line's
//count, and its accessible name.  The page computes one per shape from its live status, so a push redraws its own shape alone.
export type FloorShapeView = { state:OeeFloorState; oee:string; availability:string; performance:string; quality:string; count?:string; aria:string };
export type FloorMapShape = { key:string; kind:'machine'|'line'; name:string; points:OeePoint[]; box:OeeBox; view:Signal<FloorShapeView> };

//The floor as an SVG whose viewBox is the floor in metres:  wheel and pinch zoom about the pointer, drag pans, Fit and the
//buttons reset and step the view.  It draws bottom to top the zones, the aisles, the walls, the lines, the machines and the
//labels.  Every outline is a vector-effect stroke, so it keeps its screen width at any zoom, and the text is sized in metres
//from the pixels a metre is, so it keeps its screen size too;  a shape too small on the screen for its figures and state line
//keeps its name and its OEE (floorTextLevel).  A shape's outline colour is its state's token, a custom property of the sheet, and
//its dash the state's;  the state is in the text as well.  Shapes are focusable in drawing order, each a button named by its
//machine, its state and its OEE:  entering one, by pointer or focus, is `enter`, leaving it `leave`, and a click or Enter `pick`.
@Component({
	selector: 'oee-floor-map',
	templateUrl: './oee-floor-map.html',
	styleUrl: './oee-floor-map.scss',
	imports: [MatButtonModule, MatIconModule, MatTooltipModule]
})
export class OeeFloorMap implements AfterViewInit, OnDestroy{
	floor = input.required<OeeFloor>();
	drawing = input.required<FloorDrawing>();
	shapes = input.required<FloorMapShape[]>();//the lines, then the machines, in drawing order
	selected = input<string|undefined>();
	enter = output<string>();
	leave = output<void>();
	pick = output<string>();

	//the view, in metres, and the pixels the svg has:  a metre is their ratio under `meet`
	view = signal<OeeBox>( {x: 0, y: 0, width: 1, height: 1} );
	size = signal( {width: 800, height: 450} );
	readonly pxPerMetre = computed( ()=>{ const v = this.view(), s = this.size(); return Math.min( s.width/v.width, s.height/v.height ); } );
	readonly viewBox = computed( ()=>{ const v = this.view(); return `${v.x} ${v.y} ${v.width} ${v.height}`; } );
	constructor(){
		effect( ()=>{//another floor:  the whole of it
			this.floor();
			untracked( ()=>this.fit() );
		} );
	}
	//a screen size in metres, for text and offsets
	fs( px:number ):number{ return px/this.pxPerMetre(); }
	textLevel( box:OeeBox ){ return floorTextLevel( box, this.pxPerMetre() ); }
	points( p:OeePoint[] ):string{ return p.map( ( [x, y] )=>`${x},${y}` ).join( ' ' ); }
	//the whole floor, a grid's margin around it
	fit():void{
		const f = this.floor(), pad = f.grid;
		this.view.set( {x: -pad, y: -pad, width: f.width+2*pad, height: f.height+2*pad} );
	}
	//`factor` times closer, about `at` in metres, or the view's centre;  held between a sixteenth of the floor and three of it
	zoom( factor:number, at?:{x:number; y:number} ):void{
		const v = this.view(), f = this.floor(), longest = Math.max( f.width, f.height );
		const width = Math.min( longest*3, Math.max( longest/16, v.width/factor ) ), real = v.width/width;
		const p = at ?? { x: v.x+v.width/2, y: v.y+v.height/2 };
		this.view.set( { x: p.x-( p.x-v.x )/real, y: p.y-( p.y-v.y )/real, width, height: v.height/real } );
	}
	wheel( e:WheelEvent ):void{
		e.preventDefault();
		this.zoom( Math.exp( -e.deltaY*0.0015 ), this.#metres( e ) );
	}
	//Pointers:  one drags the view, two pinch it.  A press that moved past a few pixels is no click on the shape under it.
	down( e:PointerEvent ):void{
		if( e.button!=0 && e.pointerType=='mouse' )
			return;
		this.#pointers.set( e.pointerId, {x: e.clientX, y: e.clientY} );
		this.#moved = 0;
		this.svg()?.nativeElement.setPointerCapture( e.pointerId );
	}
	move( e:PointerEvent ):void{
		const was = this.#pointers.get( e.pointerId );
		if( !was )
			return;
		const now = {x: e.clientX, y: e.clientY}, scale = this.pxPerMetre();
		if( this.#pointers.size==1 ){
			this.#moved += Math.abs( now.x-was.x )+Math.abs( now.y-was.y );
			this.view.update( v=>({ ...v, x: v.x-( now.x-was.x )/scale, y: v.y-( now.y-was.y )/scale }) );
		}
		else{
			const other = [...this.#pointers.entries()].find( ( [id] )=>id!=e.pointerId )![1];
			const before = Math.hypot( was.x-other.x, was.y-other.y ), after = Math.hypot( now.x-other.x, now.y-other.y );
			this.#moved += Math.abs( after-before );
			if( before>0 && after>0 )
				this.zoom( after/before, this.#metres( {clientX: ( now.x+other.x )/2, clientY: ( now.y+other.y )/2} ) );
		}
		this.#pointers.set( e.pointerId, now );
	}
	up( e:PointerEvent ):void{ this.#pointers.delete( e.pointerId ); }
	clicked( key:string ):void{
		if( this.#moved<OeeFloorMap.clickSlop )
			this.pick.emit( key );
	}
	static readonly clickSlop = 6;//px of movement past which a press is a drag, not a click
	//a client point in metres, under `meet`:  the drawn floor is centred in the svg
	#metres( e:{clientX:number; clientY:number} ):{x:number; y:number}{
		const svg = this.svg()?.nativeElement, v = this.view(), scale = this.pxPerMetre();
		if( !svg )
			return { x: v.x+v.width/2, y: v.y+v.height/2 };
		const r = svg.getBoundingClientRect(), left = r.left+( r.width-v.width*scale )/2, top = r.top+( r.height-v.height*scale )/2;
		return { x: v.x+( e.clientX-left )/scale, y: v.y+( e.clientY-top )/scale };
	}
	ngAfterViewInit():void{
		const el = this.svg()?.nativeElement;
		if( el && typeof ResizeObserver!='undefined' ){
			this.#observer = new ResizeObserver( entries=>{
				const r = entries[0]?.contentRect;
				if( r?.width && r.height )
					this.size.set( {width: r.width, height: r.height} );
			} );
			this.#observer.observe( el );
		}
	}
	ngOnDestroy():void{ this.#observer?.disconnect(); }
	//a fixture's text sits at its box's centre;  a label is its one point
	centre( box:OeeBox ):{x:number; y:number}{ return { x: box.x+box.width/2, y: box.y+box.height/2 }; }
	private svg = viewChild<ElementRef<SVGSVGElement>>( 'svg' );
	#pointers = new Map<number,{x:number; y:number}>();
	#moved = 0;
	#observer?:ResizeObserver;
}