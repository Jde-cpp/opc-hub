import { Component, ElementRef, Injector, OnDestroy, afterNextRender, computed, effect, inject, input, output, signal, untracked, viewChild } from '@angular/core';
import { MatAutocomplete, MatAutocompleteOrigin, MatAutocompleteTrigger } from '@angular/material/autocomplete';
import { MatOption } from '@angular/material/core';
import { MatError, MatFormField, MatPrefix } from '@angular/material/form-field';
import { MatIcon } from '@angular/material/icon';
import { MatInput } from '@angular/material/input';
import { FilterHost, FilterSuggestion, parseFilter, suggestFilter } from '../../model/ql/filter-query';

//The filter box (views spec *The filter box*):  plain text in the grammar of filter-query.ts, read on every keystroke.  A term
//it cannot read is underlined, named under the box and left out;  the list is asked for again - `filterChange`, the text - on
//Enter, on a suggestion taken and 400 ms after the last keystroke.  The underline is drawn by a copy of the text behind the
//input, since an input cannot mark a part of its own.  `Ctrl+\`, `Cmd+\` on a Mac, brings the focus to the box from anywhere on
//the page, as on a Projects view (*Keyboard*);  Escape in it closes the suggestions, and with none open clears it.
@Component({
	selector: 'filter-box',
	templateUrl: './filter-box.html',
	styleUrl: './filter-box.scss',
	imports: [MatAutocomplete, MatAutocompleteOrigin, MatAutocompleteTrigger, MatError, MatFormField, MatIcon, MatInput, MatOption, MatPrefix],
	host: { '(document:keydown)': 'onShortcut($event)' }
})
export class FilterBox implements OnDestroy{
	#injector = inject( Injector );
	constructor(){
		//The view's text, where it is not what the box sent:  another tab's, a Discard's, a chip's.  What the box sent comes back a
		//change detection later, behind any key typed since, which it must not put back.
		effect( ()=>{
			const filter = this.filter();
			untracked( ()=>{
				if( filter!=this.#sent && filter!=this.text().trim() )
					this.#show( filter );
				this.#sent = filter;
			} );
		} );
		effect( ()=>{//MatInput reads an error state from a form control alone, which the box has none of:  told here, the field colours itself and shows what is under it as an error
			const input = this.input();
			input.errorState = this.parsed().errors.length>0;
			input.stateChanges.next();
		} );
	}
	ngOnDestroy(){ this.#cancel(); }

	filter = input.required<string>();//the view's
	host = input.required<FilterHost>();
	values = input<Record<string,readonly unknown[]>>( {} );//the loaded rows' values by column, for the suggestions
	filterChange = output<string>();
	static readonly delay = 400;

	text = signal( "" );
	caret = signal( 0 );
	scroll = signal( 0 );//the input's, which the copy behind it follows
	box = viewChild.required<ElementRef<HTMLInputElement>>( 'box' );
	trigger = viewChild.required( MatAutocompleteTrigger );
	input = viewChild.required( MatInput );
	parsed = computed( ()=>parseFilter(this.text(), this.host()) );
	//the text in runs, those the box could not read marked
	marks = computed<{text:string, bad:boolean}[]>( ()=>{
		const text = this.text(), y:{text:string, bad:boolean}[] = [];
		let at = 0;
		for( const e of this.parsed().errors ){
			if( e.start>at )
				y.push( {text: text.slice(at, e.start), bad: false} );
			y.push( {text: text.slice(e.start, e.end), bad: true} );
			at = e.end;
		}
		if( at<text.length )
			y.push( {text: text.slice(at), bad: false} );
		return y;
	} );
	//why a term is not read:  the one the cursor is in, else the first
	unread = computed<string>( ()=>{
		const errors = this.parsed().errors, caret = this.caret();
		const named = errors.find( e=>e.start<=caret && caret<=e.end ) ?? errors[0];
		return !named ? "" : errors.length>1 ? `${named.message}  ${errors.length-1} more not read.` : named.message;
	} );
	suggestions = computed<FilterSuggestion[]>( ()=>suggestFilter(this.text(), this.caret(), this.host(), column=>this.values()[column] ?? []) );
	display = ( s:FilterSuggestion|string|null )=>typeof s=="string" ? s : s?.text ?? "";

	onInput(){
		this.text.set( this.box().nativeElement.value );
		this.onCaret();
		this.#cancel();
		this.#timer = setTimeout( ()=>this.#commit(), FilterBox.delay );
	}
	onCaret(){
		const box = this.box().nativeElement;
		this.caret.set( box.selectionStart ?? box.value.length );
		this.scroll.set( box.scrollLeft );
	}
	//Enter queries, unless it took a suggestion, which queries where its term is whole
	onEnter( event:Event ){
		if( event.defaultPrevented || ( this.trigger().panelOpen && this.trigger().activeOption ) )
			return;
		this.trigger().closePanel();
		this.#commit();
	}
	//The suggestions are the autocomplete's to close, which it does after this:  Escape with none open clears the box and asks
	onEscape(){
		if( this.trigger().panelOpen || !this.text() )
			return;
		this.box().nativeElement.value = "";
		this.text.set( "" );
		this.onCaret();
		this.#commit();
	}
	//One box takes it:  the first on the page that is drawn, a pick list's on a tab not shown being passed over
	onShortcut( event:KeyboardEvent ){
		if( !( event.ctrlKey || event.metaKey ) || event.altKey || ( event.key!="\\" && event.code!="Backslash" ) || event.defaultPrevented )
			return;
		const box = this.box().nativeElement;
		if( box.checkVisibility?.()===false )
			return;
		event.preventDefault();
		box.focus();
		box.select();
	}
	//the box holds the suggestion's text already (display)
	onTaken( taken:FilterSuggestion ){
		this.text.set( taken.text );
		this.box().nativeElement.setSelectionRange( taken.caret, taken.caret );
		this.onCaret();
		this.#cancel();
		if( taken.more )//a field's name or a sign:  its values next
			afterNextRender( ()=>this.trigger().openPanel(), {injector: this.#injector} );
		else
			this.#commit();
	}
	//Save's:  what is typed and not yet asked for, asked for now, so that it is saved
	flush(){
		if( this.#timer!==undefined )
			this.#commit();
	}
	//Discard's:  what is typed and not yet asked for, dropped with the draft
	revert(){
		this.#show( this.filter() );
	}
	#commit(){
		this.#cancel();
		const text = this.text().trim();
		if( text==this.#sent )
			return;
		this.#sent = text;
		this.filterChange.emit( text );
	}
	#cancel(){
		clearTimeout( this.#timer );
		this.#timer = undefined;
	}
	//the view's text in place of what was typed, the copy behind the input taking the input's scroll once it is drawn
	#show( text:string ){
		this.#cancel();
		this.text.set( text );
		this.caret.set( text.length );
		afterNextRender( ()=>this.scroll.set( this.box().nativeElement.scrollLeft ), {injector: this.#injector} );
	}
	#sent = "";//the text the view holds, or is about to
	#timer:ReturnType<typeof setTimeout>|undefined;
}