import { Component, signal } from '@angular/core';
import { TestBed } from '@angular/core/testing';
import { MatCheckbox } from '@angular/material/checkbox';
import { MatMenuModule } from '@angular/material/menu';
import { MatTableModule } from '@angular/material/table';
import { KeyGrid } from './key-grid';
import { Paginator } from '../paginator/paginator';
import { flush, stable } from 'jde-spa/testing';

//#597:  a hand-built table as a keyboard grid - a column of checkboxes, a link, a plain cell that gains a control, a cell of two
//buttons and a row menu, as a node's Children and the watch list have between them.
@Component({
	imports: [KeyGrid, MatCheckbox, MatMenuModule, MatTableModule],
	template: `<mat-table [dataSource]="rows()" keyGrid [pager]="pager()" (rowSelect)="selected($event)">
		<ng-container matColumnDef="select">
			<mat-header-cell *matHeaderCellDef><mat-checkbox aria-label="all" (change)="all($event.checked)"/></mat-header-cell>
			<mat-cell *matCellDef="let r"><mat-checkbox [aria-label]="r.name" (click)="$event.stopPropagation()" (change)="ticked(r.name, $event.checked)"/></mat-cell>
		</ng-container>
		<ng-container matColumnDef="name">
			<mat-header-cell *matHeaderCellDef> Name </mat-header-cell>
			<mat-cell *matCellDef="let r"><a [href]="'#' + r.name" (click)="$event.preventDefault(); opened(r.name)">{{ r.name }}</a></mat-cell>
		</ng-container>
		<ng-container matColumnDef="value">
			<mat-header-cell *matHeaderCellDef> Value </mat-header-cell>
			<mat-cell *matCellDef="let r">{{ r.value }}@if( locked() ){<span class="lock" role="img" tabindex="0" aria-label="locked"></span>}</mat-cell>
		</ng-container>
		<ng-container matColumnDef="actions">
			<mat-header-cell *matHeaderCellDef></mat-header-cell>
			<mat-cell *matCellDef="let r">
				<button type="button" class="edit" (click)="edited(r.name)">Edit</button>
				<button type="button" class="remove">Remove</button>
			</mat-cell>
		</ng-container>
		<ng-container matColumnDef="menu">
			<mat-header-cell *matHeaderCellDef></mat-header-cell>
			<mat-cell *matCellDef="let r"><button type="button" class="more" [matMenuTriggerFor]="menu">More</button></mat-cell>
		</ng-container>
		<mat-header-row *matHeaderRowDef="columns()"></mat-header-row>
		<mat-row *matRowDef="let row; columns: columns()" (click)="clicked(row.name)"></mat-row>
	</mat-table>
	<mat-menu #menu="matMenu"><button mat-menu-item type="button">Watch</button></mat-menu>`
})
class Host{
	rows = signal( named(12) );
	columns = signal( ["select", "name", "value", "actions", "menu"] );
	locked = signal( false );
	pager = signal<Paginator|undefined>( undefined );
	selected = vi.fn(); all = vi.fn(); ticked = vi.fn(); opened = vi.fn(); edited = vi.fn(); clicked = vi.fn();
}
const named = ( count:number, from=0 )=>Array.from( {length: count}, (_, i)=>({name: `n${from+i}`, value: from+i}) );

describe( 'KeyGrid', ()=>{
	//cell( row, col ):  the header row is 0.  key:  a keydown on what has the focus, true where the grid took it
	const open = async ( columns?:string[] )=>{
		TestBed.configureTestingModule( {} );
		const fixture = TestBed.createComponent( Host ), host = fixture.componentInstance;
		if( columns )
			host.columns.set( columns );
		await stable( fixture, 1 );
		const table = ( fixture.nativeElement as HTMLElement ).querySelector<HTMLElement>( 'mat-table' )!;
		const cell = ( row:number, col:number )=>table.querySelectorAll( 'mat-header-row, mat-row' )[row].children[col] as HTMLElement;
		const key = ( name:string, init:KeyboardEventInit={} )=>{
			const taken = !document.activeElement!.dispatchEvent( new KeyboardEvent('keydown', {key: name, bubbles: true, cancelable: true, ...init}) );
			fixture.detectChanges();
			return taken;
		};
		//where the focus is:  [row, col], and the control's class where it is on one in the cell
		const at = ()=>{
			const active = document.activeElement as HTMLElement, held = active.closest<HTMLElement>( 'mat-header-cell, mat-cell' );
			if( !held || !table.contains(held) )
				return undefined;
			const row = [...table.querySelectorAll( 'mat-header-row, mat-row' )].indexOf( held.parentElement! ), col = [...held.parentElement!.children].indexOf( held );
			return active===held ? [row, col] : [row, col, active.className];
		};
		const stops = ()=>[...table.querySelectorAll<HTMLElement>( '*' )].filter( e=>e.tabIndex>=0 );
		return { fixture, host, table, cell, key, at, stops };
	};

	it( 'is a grid of one tab stop, its first cell, the controls in its cells out of the tab order', async ()=>{
		const { table, cell, stops } = await open();
		expect( [table.getAttribute( "role" ), cell( 0, 1 ).getAttribute( "role" ), cell( 1, 1 ).getAttribute( "role" )] ).toEqual( ["grid", "columnheader", "gridcell"] );
		expect( stops() ).toEqual( [cell( 0, 0 )] );
		expect( table.querySelectorAll( 'a[tabindex="-1"], button[tabindex="-1"], input[tabindex="-1"]' ).length ).toBe( 13+12+12*3 );
	} );

	it( 'moves by the arrows, Home and End, and to the corners with Ctrl, the cell reached the one stop', async ()=>{
		const { cell, key, at, stops } = await open();
		cell( 0, 0 ).focus();
		const went = ( ...keys:[string, KeyboardEventInit?][] )=>{ keys.forEach( k=>key(...k) ); return at(); };
		expect( went( ["ArrowRight"], ["ArrowDown"], ["ArrowDown"] ) ).toEqual( [2, 1] );
		expect( stops() ).toEqual( [cell( 2, 1 )] );
		expect( went( ["End"] ) ).toEqual( [2, 4] );
		expect( went( ["ArrowRight"] ) ).toEqual( [2, 4] );
		expect( went( ["Home"], ["ArrowLeft"], ["ArrowUp"] ) ).toEqual( [1, 0] );
		expect( went( ["End", {ctrlKey: true}] ) ).toEqual( [12, 4] );
		expect( went( ["ArrowDown"] ) ).toEqual( [12, 4] );
		expect( went( ["Home", {metaKey: true}], ["ArrowUp"] ) ).toEqual( [0, 0] );
		expect( key( "a" ) ).toBe( false );
	} );

	it( 'moves ten rows on PageDown and PageUp where the table has no pager, and turns the page of one it has', async ()=>{
		const { host, cell, key, at } = await open();
		cell( 1, 2 ).focus();
		key( "PageDown" );
		expect( at() ).toEqual( [11, 2] );
		key( "PageDown" );
		expect( at() ).toEqual( [12, 2] );
		key( "PageUp" );
		expect( at() ).toEqual( [2, 2] );
		const turn = vi.fn();
		host.pager.set( <Paginator><unknown>{turn} );
		TestBed.tick();
		key( "PageDown" );
		key( "PageUp" );
		expect( [turn.mock.calls, at()] ).toEqual( [[[1], [-1]], [2, 2]] );
	} );

	it( 'clicks on Enter what the cell holds, else the link of its row, and goes into a cell that holds several', async ()=>{
		const { host, cell, key, at } = await open();
		cell( 3, 2 ).focus();
		key( "Enter" );
		expect( [host.opened.mock.calls, host.clicked.mock.calls] ).toEqual( [[["n2"]], [["n2"]]] );//the link's click reaches the row, as a pointer's
		cell( 3, 1 ).focus();
		key( "Enter" );
		expect( host.opened.mock.calls ).toEqual( [["n2"], ["n2"]] );
		cell( 3, 3 ).focus();
		key( "Enter" );
		expect( [at(), host.edited.mock.calls] ).toEqual( [[3, 3, "edit"], []] );
		expect( key( "Tab" ) ).toBe( true );
		expect( at() ).toEqual( [3, 3, "remove"] );
		expect( key( "Tab" ) ).toBe( false );//past the last:  the browser's, out of the grid
		expect( key( "Tab", {shiftKey: true}) ).toBe( true );
		expect( at() ).toEqual( [3, 3, "edit"] );
		key( "Escape" );
		expect( at() ).toEqual( [3, 3] );
		key( "F2" );
		expect( at() ).toEqual( [3, 3, "edit"] );
		key( "ArrowDown" );//an arrow is no key of a button's
		expect( at() ).toEqual( [4, 3] );
	} );

	it( 'clicks the row on Enter where it has no link', async ()=>{
		const { host, cell, key } = await open( ["value", "actions"] );
		cell( 2, 0 ).focus();
		key( "Enter" );
		expect( host.clicked.mock.calls ).toEqual( [["n1"]] );
		cell( 0, 0 ).focus();
		key( "Enter" );//a header with nothing in it
		expect( host.clicked ).toHaveBeenCalledTimes( 1 );
	} );

	it( "ticks the row's checkbox on Space, the header's for them all, and ticks as Shift+Down and Shift+Up go", async ()=>{
		const { host, cell, key, at } = await open();
		cell( 2, 2 ).focus();
		expect( key( " " ) ).toBe( true );
		expect( host.ticked.mock.calls ).toEqual( [["n1", true]] );
		key( "ArrowDown", {shiftKey: true} );
		key( "ArrowDown", {shiftKey: true} );
		expect( [at(), host.ticked.mock.calls] ).toEqual( [[4, 2], [["n1", true], ["n2", true], ["n3", true]]] );
		key( " ", {shiftKey: true} );
		expect( [host.ticked.mock.calls.at( -1 ), host.selected.mock.calls] ).toEqual( [["n3", false], []] );
		cell( 1, 2 ).focus();
		key( "ArrowUp", {shiftKey: true} );//into the header, whose box is every row's:  not ticked
		expect( [at(), host.ticked.mock.calls.at( -1 ), host.all.mock.calls] ).toEqual( [[0, 2], ["n0", true], []] );
		key( " " );
		expect( host.all.mock.calls ).toEqual( [[true]] );
	} );

	it( "says a row's place on Shift+Space where the table has no checkboxes, and nothing for the header", async ()=>{
		const { host, cell, key } = await open( ["name", "value"] );
		cell( 3, 1 ).focus();
		expect( key( " " ) ).toBe( true );//never the page's scroll
		key( " ", {shiftKey: true} );
		cell( 0, 1 ).focus();
		key( " ", {shiftKey: true} );
		expect( host.selected.mock.calls ).toEqual( [[2]] );
	} );

	it( 'opens a menu on Enter, and has the focus back on the cell when it closes', async ()=>{
		const { fixture, cell, key, at } = await open();
		cell( 2, 4 ).focus();
		key( "Enter" );
		await stable( fixture, 1 );
		expect( [document.querySelectorAll( '.mat-mdc-menu-item' ).length, at()] ).toEqual( [1, undefined] );
		document.querySelector<HTMLElement>( '.cdk-overlay-backdrop' )!.click();
		await stable( fixture, 1 );
		expect( at() ).toEqual( [2, 4] );
	} );

	it( 'takes a control drawn later out of the tab order', async ()=>{
		const { fixture, host, cell, stops } = await open();
		host.locked.set( true );
		await stable( fixture, 1 );
		expect( [cell( 1, 2 ).querySelector( '.lock' )!.getAttribute( "tabindex" ), stops()] ).toEqual( ["-1", [cell( 0, 0 )]] );
	} );

	it( 'keeps the focus at its place when the rows are drawn again, and through a page that comes after an empty table', async ()=>{
		const { fixture, host, cell, at, stops } = await open();
		cell( 5, 2 ).focus();
		host.rows.set( named(12, 12) );
		await stable( fixture, 1 );
		expect( [at(), cell( 5, 2 ).textContent, stops()] ).toEqual( [[5, 2], "16", [cell( 5, 2 )]] );
		host.rows.set( [] );
		await stable( fixture, 1 );
		expect( at() ).toEqual( [0, 2] );
		host.rows.set( named(12, 24) );
		await stable( fixture, 1 );
		expect( at() ).toEqual( [5, 2] );
	} );

	it( 'leaves the focus where it went when the rows are drawn again', async ()=>{
		const { fixture, host, cell, at } = await open();
		cell( 5, 2 ).focus();
		cell( 5, 2 ).blur();
		await flush();//the blur is read once its task is done:  a cell taken out says the same
		host.rows.set( named(12, 12) );
		await stable( fixture, 1 );
		await flush();
		expect( [at(), document.activeElement] ).toEqual( [undefined, document.body] );
	} );
} );