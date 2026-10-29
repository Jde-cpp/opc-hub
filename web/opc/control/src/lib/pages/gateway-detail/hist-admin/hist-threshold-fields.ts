import { Component, computed, input, model } from '@angular/core';
import { MatFormFieldModule } from '@angular/material/form-field';
import { MatInputModule } from '@angular/material/input';
import { MatSelectModule } from '@angular/material/select';
import { MatTooltipModule } from '@angular/material/tooltip';
import { deviationFormats, histThresholdColumns, HistThresholdFields, HistThresholdName, histThresholdProblem } from '../../../model/hist';

//The five threshold columns as a form (spec *Templates*):  a group's defaults, a template member's own values, a node's override in
//the Thresholds dialog.  Text, blank where the row inherits (model/hist.ts HistThresholdFields).  `hints` says what a blank field
//means per column, and a column it leaves out has none;  the column's own explanation is a tooltip on the control.  `blank` is the
//word a blank field shows;  `disabledFields` are off on their own, as an enumeration's deviation is.  Two rows by meaning:  the
//deviation and its format, then the intervals and stepped - one column on a narrow form.
@Component({
	selector: 'hist-threshold-fields',
	template: `
		<div class="fields">
			@for( column of columns; track column.name ){
				<!--dynamic:  a hint wraps in a narrow cell, and the default one-line subscript let it spill past the form - a scrollbar on
					the tab that holds it.  floatLabel:  a blank input shows blank(), as a select does-->
				<mat-form-field subscriptSizing="dynamic" floatLabel="always" [class.wide]="deviation.includes(column.name)">
					<mat-label>{{ column.label }}</mat-label>
					@switch( column.kind ){
						@case( 'format' ){
							<mat-select [value]="fields()[column.name]" (selectionChange)="set(column.name, $event.value)" [disabled]="off(column.name)" [matTooltip]="column.hint">
								<mat-option value="">{{ blank() }}</mat-option>
								@for( format of formats; track $index ){
									<mat-option [value]="$index + ''">{{ format }}</mat-option>
								}
							</mat-select>
						}
						@case( 'boolean' ){
							<mat-select [value]="fields()[column.name]" (selectionChange)="set(column.name, $event.value)" [disabled]="off(column.name)" [matTooltip]="column.hint">
								<mat-option value="">{{ blank() }}</mat-option>
								<mat-option value="true">true</mat-option>
								<mat-option value="false">false</mat-option>
							</mat-select>
						}
						@default{
							<!--text, not number:  a number input reports a partial entry as blank, which the binding would write back over it-->
							<input matInput type="text" inputmode="decimal" [value]="fields()[column.name]" (input)="set(column.name, $any($event.target).value)" [placeholder]="blank()" [disabled]="off(column.name)" [matTooltip]="column.hint">
						}
					}
					@if( column.unit ){ <span matTextSuffix>{{ column.unit }}</span> }
					@if( hints()[column.name]; as hint ){ <mat-hint>{{ hint }}</mat-hint> }
				</mat-form-field>
			}
		</div>
		@if( problem(); as p ){
			<p class="problem" role="alert">{{ p }}</p>
		}`,
	styles: `
		:host{ display: block; container-type: inline-size; }
		.fields{
			display: grid;
			grid-template-columns: repeat( 6, minmax(0, 1fr) );
			gap: .5rem .75rem;
			mat-form-field{ width: 100%; grid-column: span 2; }
			.wide{ grid-column: span 3; }
		}
		@container (width < 440px){
			.fields{
				grid-template-columns: minmax(0, 1fr);
				mat-form-field, .wide{ grid-column: auto; }
			}
		}
		.problem{
			margin: 0;
			color: var(--mat-sys-error);
			font: var(--mat-sys-body-small);
		}`,
	imports: [MatFormFieldModule, MatInputModule, MatSelectModule, MatTooltipModule]
})
export class HistThresholdFieldsEditor{
	fields = model.required<HistThresholdFields>();
	disabled = input( false );
	disabledFields = input<readonly HistThresholdName[]>( [] );
	hints = input<Partial<Record<HistThresholdName,string>>>( {} );
	blank = input( 'Inherit' );
	readonly columns = histThresholdColumns;
	readonly formats = deviationFormats;
	readonly deviation:readonly HistThresholdName[] = ['exceptionDeviation', 'exceptionDeviationFormat'];
	problem = computed( ()=>histThresholdProblem( this.fields() ) );
	set( name:HistThresholdName, text:string ){ this.fields.update( f=>({...f, [name]: text}) ); }
	off( name:HistThresholdName ):boolean{ return this.disabled() || this.disabledFields().includes( name ); }
}