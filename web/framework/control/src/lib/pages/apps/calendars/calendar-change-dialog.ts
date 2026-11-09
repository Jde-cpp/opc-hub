import { Component, computed, inject, signal } from '@angular/core';
import { form, FormField } from '@angular/forms/signals';
import { MatButtonModule } from '@angular/material/button';
import { MAT_DIALOG_DATA, MatDialog, MatDialogActions, MatDialogClose, MatDialogContent, MatDialogTitle } from '@angular/material/dialog';
import { MatFormFieldModule } from '@angular/material/form-field';
import { MatIconModule } from '@angular/material/icon';
import { MatInputModule } from '@angular/material/input';
import { CalendarPattern, isDate } from '../../../model/calendar';
import { CalendarDialog } from './calendar-dialog';

//`source`:  the pattern copied, as the page names it.  `from`:  the day offered.  `save` copies it to a pattern in effect from
//the day picked and answers the copy;  its throw is the server's refusal, said on the form.
export type CalendarChangeDialogData = { source:string; from:string; zone:string; save:( date:string )=>Promise<CalendarPattern> };

//Change from… (#397, spec *Web*):  the day a change of hours takes effect.  The pattern showing is copied to a new one from that
//production day - copyCalendarPattern, the pattern and its shifts in one call - for its shifts to be edited, and the reports
//before it keep the shifts they had.
@Component({
	templateUrl: './calendar-change-dialog.html',
	styleUrl: './calendar-dialog.scss',
	imports: [FormField, MatButtonModule, MatDialogActions, MatDialogClose, MatDialogContent, MatDialogTitle, MatFormFieldModule, MatIconModule, MatInputModule]
})
export class CalendarChangeDialog extends CalendarDialog<CalendarPattern>{
	//the copy, once made
	static open( dialog:MatDialog, data:CalendarChangeDialogData ):Promise<CalendarPattern|undefined>{
		const ref = dialog.open( CalendarChangeDialog, {data, width: '440px', maxWidth: 'calc(100vw - 16px)', ariaModal: true} );
		return new Promise<CalendarPattern|undefined>( resolve=>ref.afterClosed().subscribe( y=>resolve( y && typeof y=='object' ? y : undefined ) ) );
	}
	data = inject<CalendarChangeDialogData>( MAT_DIALOG_DATA );
	model = signal( {date: this.data.from} );
	f = form( this.model );
	problem = computed( ()=>isDate( this.model().date ) ? undefined : 'Pick the day the change takes effect.' );

	async save():Promise<void>{
		if( !this.problem() )
			await this.send( ()=>this.data.save( this.model().date ) );
	}
}