import { AfterViewInit, Component, effect, ElementRef, input, OnDestroy, output, Signal, untracked, viewChild } from '@angular/core';
import { MatButtonModule } from '@angular/material/button';
import { MatIconModule } from '@angular/material/icon';
import { MatTooltipModule } from '@angular/material/tooltip';
import { FloorDrawing, floorTextLevel, OeeBox, OeeFloor, OeeFloorState, OeePoint } from '../../../model/oee-floor';
import { FloorViewport } from './oee-floor-viewport';

//A placed machine or line as the map draws it (#481, docs/demo/spec.md *Drawing*):  its state, its figures as text, a line's
//count, and its accessible name.  The page computes one per shape from its live status, so a push redraws its own shape alone.
export type FloorShapeView = { state:OeeFloorState; oee:string; availability:string; performance:string; quality:string; count?:string; aria:string };
export type FloorMapShape = { key:string; kind:'machine'|'line'; name:string; points:OeePoint[]; box:OeeBox; view:Signal<FloorShapeView> };

//The floor as an SVG whose viewBox is the floor in metres:  wheel and pinch zoom about the pointer, drag pans, Fit and the
//buttons reset and step the view.  It draws bottom to top the zones, the aisles, the walls, the lines, the machines, the lines'
//text, over the machines a boxed line holds, and the labels.  Every outline is a vector-effect stroke, so it keeps its screen width at any zoom, and the text is sized in metres
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
	readonly port = new FloorViewport();
	view = this.port.view;
	size = this.port.size;
	readonly pxPerMetre = this.port.pxPerMetre;
	readonly viewBox = this.port.viewBox;
	constructor(){
		effect( ()=>{//another floor:  the whole of it
			this.floor();
			untracked( ()=>this.fit() );
		} );
	}
	//a screen size in metres, for text and offsets
	fs( px:number ):number{ return this.port.fs( px ); }
	textLevel( box:OeeBox ){ return floorTextLevel( box, this.pxPerMetre() ); }
	points( p:OeePoint[] ):string{ return p.map( ( [x, y] )=>`${x},${y}` ).join( ' ' ); }
	//the whole floor, a grid's margin around it
	fit():void{ this.port.fit( this.floor() ); }
	//`factor` times closer, about `at` in metres, or the view's centre;  held between a sixteenth of the floor and three of it
	zoom( factor:number, at?:{x:number; y:number} ):void{ this.port.zoom( factor, this.floor(), at ); }
	wheel( e:WheelEvent ):void{ this.port.wheel( e, this.floor(), this.svg()?.nativeElement ); }
	//Pointers:  one drags the view, two pinch it.  A press that moved past a few pixels is no click on the shape under it, and
	//only then does the svg capture the pointer, so the drag pans past its edge:  a capture at the press would send the click to
	//the svg, and never to the shape.
	down( e:PointerEvent ):void{
		if( e.button!=0 && e.pointerType=='mouse' )
			return;
		this.#pointers.set( e.pointerId, {x: e.clientX, y: e.clientY} );
		this.#moved = 0;
	}
	move( e:PointerEvent ):void{
		if( !this.#pointers.has( e.pointerId ) )
			return;
		if( e.pointerType=='mouse' && !e.buttons )//released outside the svg before it captured the pointer
			return this.up( e );
		const now = {x: e.clientX, y: e.clientY}, svg = this.svg()?.nativeElement;
		this.#moved += this.port.drag( this.#pointers, e.pointerId, now, this.floor(), svg );
		this.#pointers.set( e.pointerId, now );
		if( this.#moved>=OeeFloorMap.clickSlop && svg && !svg.hasPointerCapture( e.pointerId ) )
			svg.setPointerCapture( e.pointerId );
	}
	up( e:PointerEvent ):void{ this.#pointers.delete( e.pointerId ); }
	clicked( key:string ):void{
		if( this.#moved<OeeFloorMap.clickSlop )
			this.pick.emit( key );
	}
	static readonly clickSlop = 6;//px of movement past which a press is a drag, not a click
	ngAfterViewInit():void{ this.#unobserve = this.port.observe( this.svg()?.nativeElement ); }
	ngOnDestroy():void{ this.#unobserve?.(); }
	//a fixture's text sits at its box's centre;  a label is its one point
	centre( box:OeeBox ):{x:number; y:number}{ return { x: box.x+box.width/2, y: box.y+box.height/2 }; }
	private svg = viewChild<ElementRef<SVGSVGElement>>( 'svg' );
	#pointers = new Map<number,{x:number; y:number}>();
	#moved = 0;
	#unobserve?:()=>void;
}