import { Component, computed, inject, signal } from '@angular/core';
import { form, FormField } from '@angular/forms/signals';
import { MatButtonModule } from '@angular/material/button';
import { MAT_DIALOG_DATA, MatDialog, MatDialogActions, MatDialogClose, MatDialogContent, MatDialogRef, MatDialogTitle } from '@angular/material/dialog';
import { MatFormFieldModule } from '@angular/material/form-field';
import { MatIconModule } from '@angular/material/icon';
import { MatInputModule } from '@angular/material/input';
import { MatSelectModule } from '@angular/material/select';
import { CalendarPattern, changedArgs, CycleArgs, cycleArgsOf, CycleForm, cycleProblem, toCycleArgs, toCycleForm } from '../../../model/calendar';
import { errorMessage } from '../../../utils/errors';

//`anchor`:  the day 1 offered when a weekly pattern becomes a rotation.  `save` sends the form's columns, and its throw is the
//server's refusal, said on the form.
export type CalendarCycleDialogData = { pattern:Pick<CalendarPattern,'cycleDays'|'cycleStart'>; zone:string; dayStart:number; anchor:string; save:( args:CycleArgs )=>Promise<unknown> };

//A pattern's cycle (#397, spec *The cycle*):  the week, Monday first, or a rotation of 1 to 31 days from the date that was its day 1.
//The grid's columns are the cycle's days, and a shift's days are kept by number, so the server refuses a cycle shorter than a
//day some shift runs on.
@Component({
	templateUrl: './calendar-cycle-dialog.html',
	styleUrl: './calendar-dialog.scss',
	imports: [FormField, MatButtonModule, MatDialogActions, MatDialogClose, MatDialogContent, MatDialogTitle, MatFormFieldModule, MatIconModule, MatInputModule, MatSelectModule]
})
export class CalendarCycleDialog{
	//true once the cycle was saved
	static open( dialog:MatDialog, data:CalendarCycleDialogData ):Promise<boolean>{
		const ref = dialog.open( CalendarCycleDialog, {data, width: '440px', maxWidth: 'calc(100vw - 16px)', ariaModal: true} );
		return new Promise<boolean>( resolve=>ref.afterClosed().subscribe( y=>resolve( y===true ) ) );
	}
	data = inject<CalendarCycleDialogData>( MAT_DIALOG_DATA );
	#ref = inject( MatDialogRef<CalendarCycleDialog,boolean> );
	model = signal<CycleForm>( toCycleForm( this.data.pattern, this.data.zone, this.data.anchor ) );
	f = form( this.model );
	problem = computed( ()=>cycleProblem( this.model() ) );
	changed = computed( ()=>!!this.problem() || Object.keys( changedArgs( toCycleArgs( this.model(), this.data.dayStart ), cycleArgsOf( this.data.pattern, this.data.zone ) ) ).length>0 );
	busy = signal( false );
	error = signal<string|undefined>( undefined );

	async save():Promise<void>{
		if( this.problem() || this.busy() )
			return;
		this.busy.set( true );
		this.#ref.disableClose = true;//Escape or the backdrop would close it on a save the server may still take, and the page never read again
		this.error.set( undefined );
		try{
			await this.data.save( toCycleArgs( this.model(), this.data.dayStart ) );
			this.#ref.close( true );
		}
		catch( e ){
			this.error.set( errorMessage(e) );
		}
		finally{
			this.#ref.disableClose = false;
			this.busy.set( false );
		}
	}
}