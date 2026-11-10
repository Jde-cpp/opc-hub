import { Component } from '@angular/core';
import { TestBed } from '@angular/core/testing';
import { MatSortModule, Sort } from '@angular/material/sort';
import { MatTableModule } from '@angular/material/table';
import { LocalPrefs, ProfileStore } from 'jde-spa';
import { View } from '../../model/ql/view';
import { ViewSet } from '../../model/ql/view-set';
import { ViewColumnCell, ViewColumnHeader, ViewColumns } from './view-columns';
import { required, scalar, schemaOf } from 'jde-framework/testing';
import { stable } from 'jde-spa/testing';

//#592:  a hand-built table - the logs', a node's children, the watch list - sizes its columns in its sheet and binds no style, so
//the directive is on its cells as well as its headers:  a width where the view gives one, and nothing where the sheet has it.
const schema = schemaOf( "Entry", required("time"), scalar("message"), scalar("line") );

@Component({
	imports: [MatSortModule, MatTableModule, ViewColumnCell, ViewColumnHeader, ViewColumns],
	template: `<mat-table [dataSource]="rows" [viewColumns]="views" (columnsChange)="changes=changes+1" matSort (matSortChange)="sorts.push($event)">
		<ng-container matColumnDef="time">
			<mat-header-cell viewColumn *matHeaderCellDef> Time </mat-header-cell>
			<mat-cell viewColumn *matCellDef="let r">{{ r.time }}</mat-cell>
		</ng-container>
		<ng-container matColumnDef="message">
			<mat-header-cell viewColumn *matHeaderCellDef> Message <span class="unit">text</span></mat-header-cell>
			<mat-cell viewColumn *matCellDef="let r">{{ r.message }}</mat-cell>
		</ng-container>
		<ng-container matColumnDef="line">
			<mat-header-cell viewColumn [sortable]="false" *matHeaderCellDef> Line </mat-header-cell>
			<mat-cell viewColumn *matCellDef="let r">{{ r.line }}</mat-cell>
		</ng-container>
		<ng-container matColumnDef="actions">
			<mat-header-cell *matHeaderCellDef></mat-header-cell>
			<mat-cell *matCellDef="let r">x</mat-cell>
		</ng-container>
		<mat-header-row *matHeaderRowDef="columns()"></mat-header-row>
		<mat-row *matRowDef="let row; columns: columns()"></mat-row>
	</mat-table>`
})
class Host{
	views:ViewSet<View> = null as any;//before the first render
	rows = [{time: "10:00", message: "a", line: 1}, {time: "10:01", message: "b", line: 2}];
	columns = ()=>[...this.views.current().fields.filter( f=>f.displayed ).map( f=>f.name ), "actions"];
	changes = 0;
	sorts:Sort[] = [];
}

describe( 'ViewColumns on a hand-built table', ()=>{
	const open = async ()=>{
		const set = new ViewSet( {type: View, key: "entries", profileStore: TestBed.inject( ProfileStore ), prefs: new LocalPrefs()} );
		set.reset( [View.fromTable({columns: ["time", "message", "line"], sort: "time"}, schema)], schema );
		const fixture = TestBed.createComponent( Host );
		fixture.componentInstance.views = set;
		await stable( fixture, 1 );
		const element = fixture.nativeElement as HTMLElement;
		const header = ( name:string )=>element.querySelector<HTMLElement>( `mat-header-cell.mat-column-${name}` )!;
		const cells = ( name:string )=>[...element.querySelectorAll<HTMLElement>( `mat-cell.mat-column-${name}` )];
		const pick = async ( name:string, entry:string )=>{
			header( name ).querySelector<HTMLButtonElement>( 'button.column-menu' )!.click();
			await stable( fixture, 1 );
			[...[...document.querySelectorAll( '.mat-mdc-menu-panel' )].at( -1 )!.querySelectorAll<HTMLButtonElement>( '.mat-mdc-menu-item' )].find( i=>i.textContent!.includes( entry ) )!.click();
			await stable( fixture, 1 );
		};
		return { fixture, host: fixture.componentInstance, set, element, header, cells, pick };
	};
	beforeEach( ()=>localStorage.clear() );

	it( "leaves a column to its sheet until the view gives it a width, then sets it on the header and every cell, and takes it off again", async ()=>{
		const { fixture, header, cells, pick, set, host } = await open();
		const drawn = ()=>[header( "message" ), ...cells( "message" )].map( e=>[e.style.flex, e.style.minWidth] );
		expect( drawn() ).toEqual( [["", ""], ["", ""], ["", ""]] );
		await pick( "message", "Wider" );
		expect( drawn() ).toEqual( [["0 0 64px", "0px"], ["0 0 64px", "0px"], ["0 0 64px", "0px"]] );//from nothing laid out, so the floor;  over any minimum the sheet has
		expect( [header( "time" ).style.flex, set.current().isAdhoc, host.changes] ).toEqual( ["", true, 1] );
		header( "message" ).querySelector( '.column-resize' )!.dispatchEvent( new MouseEvent( 'dblclick', {bubbles: true} ) );
		await stable( fixture, 1 );
		expect( drawn() ).toEqual( [["", ""], ["", ""], ["", ""]] );
		expect( set.current().isAdhoc ).toBe( false );
	} );

	it( 'keeps what the host wrote in a header, and gives a column outside the view no menu', async ()=>{
		const { header, element } = await open();
		expect( header( "message" ).querySelector( '.column-label .unit' )!.textContent ).toBe( "text" );
		expect( header( "actions" ).querySelector( '.column-menu, .column-resize, .column-label' ) ).toBeNull();
		expect( element.querySelectorAll( 'button.column-menu' ).length ).toBe( 3 );
	} );

	it( 'sorts through the table, but for a column that does not, which says no sort', async ()=>{
		const { fixture, header, host } = await open();
		header( "message" ).querySelector<HTMLElement>( '.mat-sort-header-container' )!.click();
		header( "line" ).querySelector<HTMLElement>( '.mat-sort-header-container' )!.click();
		await stable( fixture, 1 );
		expect( host.sorts ).toEqual( [{active: "message", direction: "asc"}] );
		expect( [header( "time" ).getAttribute( "aria-sort" ), header( "message" ).getAttribute( "aria-sort" ), header( "line" ).hasAttribute( "aria-sort" )] ).toEqual( ["ascending", "none", false] );
	} );

	it( 'hides and moves a column, the cells following, and pins the first where there is no name', async ()=>{
		const { element, pick, cells } = await open();
		const order = ()=>[...element.querySelectorAll( 'mat-header-cell' )].map( h=>[...h.classList].find( c=>c.startsWith( "mat-column-" ) )!.slice( 11 ) );
		expect( [...element.querySelectorAll( '.jde-pinned' )].every( e=>e.classList.contains( "mat-column-time" ) ) ).toBe( true );
		await pick( "time", "Move right" );
		expect( order() ).toEqual( ["message", "time", "line", "actions"] );
		expect( element.querySelector( 'mat-row' )!.textContent ).toBe( "a10:001x" );
		expect( cells( "message" ).every( c=>c.classList.contains( "jde-pinned" ) ) ).toBe( true );
		await pick( "message", "Hide field" );
		expect( order() ).toEqual( ["time", "line", "actions"] );
	} );
} );