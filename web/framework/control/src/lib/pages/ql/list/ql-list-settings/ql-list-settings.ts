import { Component, OnInit, input, signal, computed, output, viewChild } from '@angular/core';
import { MatButtonModule } from '@angular/material/button';
import { MatIcon } from '@angular/material/icon';
import { MatInputModule } from '@angular/material/input';
import { MatSelectModule } from '@angular/material/select';
import { MatToolbar } from '@angular/material/toolbar';
import { QLListSettingsSort } from './ql-list-settings-sort/ql-list-settings-sort';
import { View } from '../../../../model/ql/view';
import { TableSchema } from '../../../../model/ql/schema/table-schema';

@Component({
	selector: 'ql-list-settings',
	templateUrl: './ql-list-settings.html',
	imports: [MatButtonModule, MatIcon, MatInputModule, MatSelectModule, MatToolbar, QLListSettingsSort]
})
export class QLListSettings implements OnInit{
	ngOnInit(){
		this.name.set( this.originalName() );
	}

	getView( isAdhoc:boolean ):View{
		let view = View.copy( this.view() );//copy — mutating the profile's own instance made onViewSave's duplicate-check self-match, so views were never promoted to User/persisted
		if( !isAdhoc && this.name()!=this.originalName() ){//another name is another view:  ViewSet.save adds the one that has no number, and names may repeat
			view.name = this.name();
			view.number = 0;
		}
		view.sort = this.sort().dataSource.filter( s=>s.active ).map( s=>({active: s.active, direction: s.direction}) );
		return view;
	}

	onNameInput( event:Event ){ this.name.set( (<HTMLInputElement>event.target).value ); }
	onNameFocus( event:FocusEvent ){ (<HTMLInputElement>event.target).select(); }//was `$event.target.select()` in the template, which type-checks only because EventTarget is loose there
	excludedColumns = input<string[]>([]);
	canDelete = input<boolean>( false );//ViewSet.canRemove, of the view
	schema = input.required<TableSchema>();
	view = input.required<View>();
	originalName = computed( () =>{return this.view().name ?? "<default>" + (this.view().isAdhoc ? "*" : "");} );
	onSave = output<View>();
	onShow = output<View>();
	onDelete = output<View>();
	onCancel = output<void>();

	sort = viewChild.required( QLListSettingsSort );

	name = signal<string>( null as any );
}