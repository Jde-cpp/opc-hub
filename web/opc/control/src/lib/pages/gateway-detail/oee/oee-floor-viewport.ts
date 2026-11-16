import { computed, signal } from '@angular/core';
import { OeeBox } from '../../../model/oee-floor';

//The view of a floor drawn as an SVG whose viewBox is in metres (#481), the map's and the editor's (#482):  the box in view,
//the pixels the svg has, and so the pixels a metre is under `meet`;  Fit, a zoom about a point held between a sixteenth of the
//floor's longest side and three of it, the wheel's zoom about the pointer, and the pointers' drag - one pans, two pinch.
export type FloorRoom = { width:number; height:number; grid:number };
type Pixel = { x:number; y:number };
export class FloorViewport{
	view = signal<OeeBox>( {x: 0, y: 0, width: 1, height: 1} );
	size = signal( {width: 800, height: 450} );//the svg's, where there is no ResizeObserver to say
	readonly pxPerMetre = computed( ()=>{ const v = this.view(), s = this.size(); return Math.min( s.width/v.width, s.height/v.height ); } );
	readonly viewBox = computed( ()=>{ const v = this.view(); return `${v.x} ${v.y} ${v.width} ${v.height}`; } );
	//a screen size in metres, for text and offsets
	fs( px:number ):number{ return px/this.pxPerMetre(); }
	readonly centre = computed( ()=>{ const v = this.view(); return { x: v.x+v.width/2, y: v.y+v.height/2 }; } );
	//the whole floor, a grid's margin around it
	fit( floor:FloorRoom ):void{
		const pad = floor.grid;
		this.view.set( {x: -pad, y: -pad, width: floor.width+2*pad, height: floor.height+2*pad} );
	}
	//`factor` times closer, about `at` in metres, or the view's centre
	zoom( factor:number, floor:FloorRoom, at?:Pixel ):void{
		const v = this.view(), longest = Math.max( floor.width, floor.height );
		const width = Math.min( longest*3, Math.max( longest/16, v.width/factor ) ), real = v.width/width;
		const p = at ?? this.centre();
		this.view.set( { x: p.x-( p.x-v.x )/real, y: p.y-( p.y-v.y )/real, width, height: v.height/real } );
	}
	wheel( e:WheelEvent, floor:FloorRoom, svg:SVGSVGElement|undefined ):void{
		e.preventDefault();
		this.zoom( Math.exp( -e.deltaY*0.0015 ), floor, this.metres( e, svg ) );
	}
	//a client point in metres, under `meet`:  the drawn floor is centred in the svg
	metres( e:{clientX:number; clientY:number}, svg:SVGSVGElement|undefined ):Pixel{
		const v = this.view(), scale = this.pxPerMetre();
		if( !svg )
			return this.centre();
		const r = svg.getBoundingClientRect(), left = r.left+( r.width-v.width*scale )/2, top = r.top+( r.height-v.height*scale )/2;
		return { x: v.x+( e.clientX-left )/scale, y: v.y+( e.clientY-top )/scale };
	}
	//The pointer `id` of `pointers`, each where it last was, now at `now`:  alone it drags the view, with another it pinches it
	//about the middle of the two.  Answers the pixels it moved by;  the caller keeps where the pointer is.
	drag( pointers:Map<number,Pixel>, id:number, now:Pixel, floor:FloorRoom, svg:SVGSVGElement|undefined ):number{
		const was = pointers.get( id );
		if( !was )
			return 0;
		if( pointers.size==1 ){
			const scale = this.pxPerMetre();
			this.view.update( v=>({ ...v, x: v.x-( now.x-was.x )/scale, y: v.y-( now.y-was.y )/scale }) );
			return Math.abs( now.x-was.x )+Math.abs( now.y-was.y );
		}
		const other = [...pointers.entries()].find( ( [key] )=>key!=id )![1];
		const before = Math.hypot( was.x-other.x, was.y-other.y ), after = Math.hypot( now.x-other.x, now.y-other.y );
		if( before>0 && after>0 )
			this.zoom( after/before, floor, this.metres( {clientX: ( now.x+other.x )/2, clientY: ( now.y+other.y )/2}, svg ) );
		return Math.abs( after-before );
	}
	//the svg's pixels as they change;  answers the end of it
	observe( svg:SVGSVGElement|undefined ):()=>void{
		if( !svg || typeof ResizeObserver=='undefined' )
			return ()=>{};
		const observer = new ResizeObserver( entries=>{
			const r = entries[0]?.contentRect;
			if( r?.width && r.height )
				this.size.set( {width: r.width, height: r.height} );
		} );
		observer.observe( svg );
		return ()=>observer.disconnect();
	}
}