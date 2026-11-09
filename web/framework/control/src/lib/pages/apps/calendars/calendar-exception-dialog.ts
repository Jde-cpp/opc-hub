import { Component, computed, inject, signal } from '@angular/core';
import { form, FormField, required } from '@angular/forms/signals';
import { MatButtonModule } from '@angular/material/button';
import { MatCheckboxModule } from '@angular/material/checkbox';
import { MAT_DIALOG_DATA, MatDialog, MatDialogActions, MatDialogClose, MatDialogContent, MatDialogRef, MatDialogTitle } from '@angular/material/dialog';
import { MatFormFieldModule } from '@angular/material/form-field';
import { MatIconModule } from '@angular/material/icon';
import { MatInputModule } from '@angular/material/input';
import { MatSelectModule } from '@angular/material/select';
import { CalendarException, changedArgs, ExceptionArgs, exceptionArgsOf, ExceptionForm, exceptionProblem, exceptionRange, toExceptionArgs, toExceptionForm, wholeDays } from '../../../model/calendar';
import { errorMessage } from '../../../utils/errors';

//`zone` and `dayStart`:  the calendar's, as saved.  `exception`:  the one edited, none for a new one.  `save` sends the form's
//columns, and its throw is the server's refusal, said on the form.
export type CalendarExceptionDialogData = { zone:string; dayStart:number; exception?:CalendarException; save:( args:ExceptionArgs )=>Promise<unknown> };

//An exception's form (#396):  its name, whether it closes planned time or opens it, and its range - whole production days, each
//from the day's start to the next day's, or a start and an end to the minute.  Both are the calendar's wall clock and go to the
//server with no zone in them, which reads them in the calendar's (spec *Validation*).
@Component({
	templateUrl: './calendar-exception-dialog.html',
	styleUrl: './calendar-dialog.scss',
	imports: [FormField, MatButtonModule, MatCheckboxModule, MatDialogActions, MatDialogClose, MatDialogContent, MatDialogTitle, MatFormFieldModule, MatIconModule, MatInputModule, MatSelectModule]
})
export class CalendarExceptionDialog{
	//true once an exception was saved
	static open( dialog:MatDialog, data:CalendarExceptionDialogData ):Promise<boolean>{
		const ref = dialog.open( CalendarExceptionDialog, {data, width: '520px', maxWidth: 'calc(100vw - 16px)', ariaModal: true} );
		return new Promise<boolean>( resolve=>ref.afterClosed().subscribe( y=>resolve( y===true ) ) );
	}
	data = inject<CalendarExceptionDialogData>( MAT_DIALOG_DATA );
	#ref = inject( MatDialogRef<CalendarExceptionDialog,boolean> );
	model = signal<ExceptionForm>( toExceptionForm( this.data.exception, this.data.zone, this.data.dayStart ) );
	f = form( this.model, path=>{ required( path.name ); } );
	problem = computed( ()=>exceptionProblem( this.model(), this.data.zone, this.data.dayStart ) );
	//the range as it will be sent, which for whole days shows where a day begins:  of a range that is one, named yet or not
	rangeText = computed( ()=>{
		if( exceptionProblem( {...this.model(), name: 'named'}, this.data.zone, this.data.dayStart ) )
			return '';
		const range = exceptionRange( this.model(), this.data.dayStart );
		return `${range.start.replace( 'T', ' ' )} to ${range.finish.replace( 'T', ' ' )}, ${this.data.zone}`;
	} );
	changed = computed( ()=>!this.data.exception || !!this.problem() || Object.keys( changedArgs( toExceptionArgs( this.model(), this.data.dayStart ), exceptionArgsOf( this.data.exception, this.data.zone ) ) ).length>0 );
	busy = signal( false );
	error = signal<string|undefined>( undefined );

	//the range carried from one way of entering it to the other:  the days as their times, and the times as the days they touch
	setWhole( whole:boolean ):void{
		const m = this.model(), dayStart = this.data.dayStart;
		if( whole ){
			const days = wholeDays( m.start, m.finish, dayStart );
			const from = days?.from ?? m.start.substring( 0, 10 ), to = days?.to ?? m.finish.substring( 0, 10 );
			this.model.set( {...m, whole, from, to: to<from ? from : to} );
		}
		else
			this.model.set( {...m, whole, ...exceptionRange( {...m, whole: true}, dayStart )} );
	}
	async save():Promise<void>{
		if( this.problem() || this.busy() )
			return;
		this.busy.set( true );
		this.error.set( undefined );
		try{
			await this.data.save( toExceptionArgs( this.model(), this.data.dayStart ) );
			this.#ref.close( true );
		}
		catch( e ){
			this.error.set( errorMessage(e) );
		}
		finally{
			this.busy.set( false );
		}
	}
}