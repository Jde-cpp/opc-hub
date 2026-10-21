import { Component, OnInit, input, signal, computed, output, viewChild } from '@angular/core';
import { MatButtonModule } from '@angular/material/button';
import { MatIcon } from '@angular/material/icon';
import { MatInputModule } from '@angular/material/input';
import { MatSelectModule } from '@angular/material/select';
import { MatTabsModule } from '@angular/material/tabs';
import { MatToolbar } from '@angular/material/toolbar';
import { QLListSettingsFilter } from './ql-list-settings-filter/ql-list-settings-filter';
import { QLListSettingsDisplay } from './ql-list-settings-display/ql-list-settings-display';
import { QLListSettingsSort } from './ql-list-settings-sort/ql-list-settings-sort';
import { View, ViewField } from '../../../../model/ql/view';
import { persistedTabIndex } from 'jde-spa';
import { TableSchema } from '../../../../model/ql/schema/table-schema';

const tabKey = 'qlListSettings';

@Component({
	selector: 'ql-list-settings',
	templateUrl: './ql-list-settings.html',
	imports: [MatButtonModule, MatIcon, MatInputModule, MatSelectModule, MatTabsModule, MatToolbar, QLListSettingsFilter, QLListSettingsDisplay, QLListSettingsSort]
})
export class QLListSettings implements OnInit{
	ngOnInit(){
		this.name.set( this.originalName() );
	}

	getView( isAdhoc:boolean ):View{
		let view = View.copy( this.view() );//copy — mutating the profile's own instance made onViewSave's duplicate-check self-match, so views were never promoted to User/persisted
		if( !isAdhoc )
			view.name = this.name();
		let displayCols = this.display().dataSource;
		const fields = displayCols.filter( c=>c.name!="Selector" ).map( c=>new ViewField(c as ViewField) );
		for( let field of this.view().fields.filter(f=>!fields.find(c=>c.name==f.name)) )//the list only offers columns(), so excluded-but-queried fields (id/slug) would be lost
			fields.push( new ViewField(field) );
		view.fields = fields;
		view.showSelector = displayCols[0].displayed;
		view.limit = this.display().pageSize();
		view.fieldFilters = this.filter().fieldFilters();
		view.sort = this.sort().dataSource.filter( s=>s.active ).map( s=>({active: s.active, direction: s.direction}) );
		return view;
	}

	disableSave():boolean{
		return ( this.view().isSystem && this.originalName()==this.name() )
			|| ( this.originalName()==this.name() && !this.view().name )
			|| this.reservedName();
	}
	//A system view edited and saved under another system view's name stayed a System view (onViewSave), so it replaced that view
	//for the session and was never persisted - with Deleted on every list, any list could hit it.  A System view's Adhoc twin keeps
	//its name, and a Save leaves it Adhoc (ViewSet.save), so it is renamed or shown (reviews-issue/web-refactor-review.md #3).
	reservedName = computed( ()=>(this.name()!=this.view().name || this.view().isAdhoc) && this.reservedNames().includes(this.name()) );
	//Delete only ever removes a saved user view.  It used to share disableSave(), which left it ENABLED for an adhoc view —
	//and logs' onViewDelete opens with an assertion on the type, so clicking it there threw (via a `debugger;` that freezes
	//the page outright whenever DevTools/automation is attached).
	disableDelete():boolean{ return !this.view().isUser; }
	onNameInput( event:Event ){ this.name.set( (<HTMLInputElement>event.target).value ); }
	onNameFocus( event:FocusEvent ){ (<HTMLInputElement>event.target).select(); }//was `$event.target.select()` in the template, which type-checks only because EventTarget is loose there
	excludedColumns = input<string[]>([]);
	reservedNames = input<string[]>([]);//the host's system view names
	paged = input<boolean>( true );//false where the host does not page - a node's Children - so it offers no Page Size
	suggestions = input.required<Record<string,any[]>>();
	schema = input.required<TableSchema>();
	view = input.required<View>();
	columns = input.required<Record<string,string>>();
	originalName = computed( () =>{return this.view().name ?? "<default>" + (this.view().isAdhoc ? "*" : "");} );
	onSave = output<View>();
	onShow = output<View>();
	onDelete = output<View>();
	onCancel = output<void>();

	display = viewChild.required( QLListSettingsDisplay );
	filter = viewChild.required( QLListSettingsFilter );
	sort = viewChild.required( QLListSettingsSort );

	name = signal<string>( null as any );
	tabIndex = persistedTabIndex( tabKey );
}