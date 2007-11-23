
/******************************************************************************
 *
 * Module Name: dsutils - Dispatcher utilities
 *
 *****************************************************************************/

/*
 *  Copyright (C) 2000 R. Byron Moore
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */


#include "acpi.h"
#include "parser.h"
#include "amlcode.h"
#include "dispatch.h"
#include "interp.h"
#include "namesp.h"
#include "debugger.h"

#define _COMPONENT          PARSER
	 MODULE_NAME         ("dsutils");


/*****************************************************************************
 *
 * FUNCTION:    Acpi_ds_delete_result_if_not_used
 *
 * PARAMETERS:  Op
 *              Result_obj
 *              Walk_state
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Used after interpretation of an opcode.  If there is an internal
 *              result descriptor, check if the parent opcode will actually use
 *              this result.  If not, delete the result now so that it will
 *              not become orphaned.
 *
 ****************************************************************************/

void
acpi_ds_delete_result_if_not_used (
	ACPI_GENERIC_OP         *op,
	ACPI_OBJECT_INTERNAL    *result_obj,
	ACPI_WALK_STATE         *walk_state)
{
	ACPI_OP_INFO            *parent_info;
	ACPI_OBJECT_INTERNAL    *obj_desc;
	ACPI_STATUS             status;


	if (!op) {
		return;
	}

	if (!result_obj) {
		return;
	}

	if (!op->parent) {
		/*
		 * If there is no parent, the result can't possibly be used!
		 * (An executing method typically has no parent, since each
		 * method is parsed separately
		 */

		/*
		 * Must pop the result stack (Obj_desc should be equal
		 *  to Result_obj)
		 */

		status = acpi_ds_result_stack_pop (&obj_desc, walk_state);
		if (ACPI_FAILURE (status)) {
			return;
		}

		acpi_cm_remove_reference (result_obj);

		return;
	}


	/*
	 * Get info on the parent.  The root Op is AML_SCOPE
	 */

	parent_info = acpi_ps_get_opcode_info (op->parent->opcode);
	if (!parent_info) {
		return;
	}


	/* Never delete the return value associated with a return opcode */

	if (op->parent->opcode == AML_RETURN_OP) {
		return;
	}


	/*
	 * Decide what to do with the result based on the parent.  If
	 * the parent opcode will not use the result, delete the object.
	 * Otherwise leave it as is, it will be deleted when it is used
	 * as an operand later.
	 */

	switch (parent_info->flags & OP_INFO_TYPE)
	{
	/*
	 * In these cases, the parent will never use the return object,
	 * so delete it here and now.
	 */
	case OPTYPE_CONTROL:        /* IF, ELSE, WHILE only */
	case OPTYPE_NAMED_OBJECT:   /* Scope, method, etc. */

		/*
		 * Must pop the result stack (Obj_desc should be equal
		 * to Result_obj)
		 */

		status = acpi_ds_result_stack_pop (&obj_desc, walk_state);
		if (ACPI_FAILURE (status)) {
			return;
		}

		acpi_cm_remove_reference (result_obj);
		break;

	/*
	 * In all other cases. the parent will actually use the return
	 * object, so keep it.
	 */
	default:
		break;
	}

	return;
}


/*****************************************************************************
 *
 * FUNCTION:    Acpi_ds_create_operand
 *
 * PARAMETERS:  Walk_state
 *              Arg
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Translate a parse tree object that is an argument to an AML
 *              opcode to the equivalent interpreter object.  This may include
 *              looking up a name or entering a new name into the internal
 *              namespace.
 *
 ****************************************************************************/

ACPI_STATUS
acpi_ds_create_operand (
	ACPI_WALK_STATE         *walk_state,
	ACPI_GENERIC_OP         *arg)
{
	ACPI_STATUS             status = AE_OK;
	char                    *name_string;
	u32                     name_length;
	OBJECT_TYPE_INTERNAL    data_type;
	ACPI_OBJECT_INTERNAL    *obj_desc;
	ACPI_GENERIC_OP         *parent_op;
	u16                     opcode;
	u32                     flags;
	OPERATING_MODE          interpreter_mode;


	/* A valid name must be looked up in the namespace */

	if ((arg->opcode == AML_NAMEPATH_OP) &&
		(arg->value.string))
	{
		/* Get the entire name string from the AML stream */

		status = acpi_aml_get_name_string (ACPI_TYPE_ANY,
				   arg->value.buffer,
				   &name_string,
				   &name_length);

		if (ACPI_FAILURE (status)) {
			return (status);
		}

		/*
		 * All prefixes have been handled, and the name is
		 * in Name_string
		 */

		/*
		 * Differentiate between a namespace "create" operation
		 * versus a "lookup" operation (IMODE_LOAD_PASS2 vs.
		 * IMODE_EXECUTE) in order to support the creation of
		 * namespace objects during the execution of control methods.
		 */

		parent_op = arg->parent;
		if ((acpi_ps_is_named_object_op (parent_op->opcode)) &&
			(parent_op->opcode != AML_METHODCALL_OP) &&
			(parent_op->opcode != AML_NAMEPATH_OP))
		{
			/* Enter name into namespace if not found */

			interpreter_mode = IMODE_LOAD_PASS2;
		}

		else {
			/* Return a failure if name not found */

			interpreter_mode = IMODE_EXECUTE;
		}

		status = acpi_ns_lookup (walk_state->scope_info, name_string,
				 ACPI_TYPE_ANY, interpreter_mode,
				 NS_SEARCH_PARENT | NS_DONT_OPEN_SCOPE,
				 walk_state,
				 (ACPI_NAMED_OBJECT**) &obj_desc);

		/* Free the namestring created above */

		acpi_cm_free (name_string);

		/*
		 * The only case where we pass through (ignore) a NOT_FOUND
		 * error is for the Cond_ref_of opcode.
		 */

		if (status == AE_NOT_FOUND) {
			if (parent_op->opcode == AML_COND_REF_OF_OP) {
				/*
				 * For the Conditional Reference op, it's OK if
				 * the name is not found;  We just need a way to
				 * indicate this to the interpreter, set the
				 * object to the root
				 */
				obj_desc = (ACPI_OBJECT_INTERNAL *) acpi_gbl_root_object;
				status = AE_OK;
			}

			else {
				/*
				 * We just plain didn't find it -- which is a
				 * very serious error at this point
				 */
				status = AE_AML_NAME_NOT_FOUND;
			}
		}

		/* Check status from the lookup */

		if (ACPI_FAILURE (status)) {
			return (status);
		}

		/* Put the resulting object onto the current object stack */

		status = acpi_ds_obj_stack_push (obj_desc, walk_state);
		if (ACPI_FAILURE (status)) {
			return (status);
		}
	}


	else {
		/* Check for null name case */

		if (arg->opcode == AML_NAMEPATH_OP) {
			/*
			 * If the name is null, this means that this is an
			 * optional result parameter that was not specified
			 * in the original ASL.  Create an Reference for a
			 * placeholder
			 */
			opcode = AML_ZERO_OP;       /* Has no arguments! */

			/*
			 * TBD: [Investigate] anything else needed for the
			 * zero op lvalue?
			 */
		}

		else {
			opcode = arg->opcode;
		}


		/* Get the data type of the argument */

		data_type = acpi_ds_map_opcode_to_data_type (opcode, &flags);
		if (data_type == INTERNAL_TYPE_INVALID) {
			return (AE_NOT_IMPLEMENTED);
		}

		if (flags & OP_HAS_RETURN_VALUE) {
			/*
			 * Use value that was already previously returned
			 * by the evaluation of this argument
			 */

			status = acpi_ds_result_stack_pop (&obj_desc, walk_state);
			if (ACPI_FAILURE (status)) {
				/*
				 * Only error is underflow, and this indicates
				 * a missing or null operand!
				 */
				return (status);
			}

		}

		else {
			/* Create an ACPI_INTERNAL_OBJECT for the argument */

			obj_desc = acpi_cm_create_internal_object (data_type);
			if (!obj_desc) {
				return (AE_NO_MEMORY);
			}

			/* Initialize the new object */

			status = acpi_ds_init_object_from_op (walk_state, arg,
					 opcode, obj_desc);
			if (ACPI_FAILURE (status)) {
				acpi_cm_free (obj_desc);
				return (status);
			}
	   }

		/* Put the operand object on the object stack */

		status = acpi_ds_obj_stack_push (obj_desc, walk_state);
		if (ACPI_FAILURE (status)) {
			return (status);
		}

	}

	return (AE_OK);
}


/*****************************************************************************
 *
 * FUNCTION:    Acpi_ds_create_operands
 *
 * PARAMETERS:  First_arg           - First argument of a parser argument tree
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Convert an operator's arguments from a parse tree format to
 *              namespace objects and place those argument object on the object
 *              stack in preparation for evaluation by the interpreter.
 *
 ****************************************************************************/

ACPI_STATUS
acpi_ds_create_operands (
	ACPI_WALK_STATE         *walk_state,
	ACPI_GENERIC_OP         *first_arg)
{
	ACPI_STATUS             status = AE_OK;
	ACPI_GENERIC_OP         *arg;
	u32                     args_pushed = 0;


	arg = first_arg;


	/* For all arguments in the list... */

	while (arg) {

		status = acpi_ds_create_operand (walk_state, arg);
		if (ACPI_FAILURE (status)) {
			goto cleanup;
		}

		/* Move on to next argument, if any */

		arg = arg->next;
		args_pushed++;
	}

	return (status);


cleanup:
	/*
	 * We must undo everything done above; meaning that we must
	 * pop everything off of the operand stack and delete those
	 * objects
	 */

	acpi_ds_obj_stack_pop_and_delete (args_pushed, walk_state);

	return (status);
}


/*****************************************************************************
 *
 * FUNCTION:    Acpi_ds_resolve_operands
 *
 * PARAMETERS:  Walk_state          - Current walk state with operands on stack
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Resolve all operands to their values.  Used to prepare
 *              arguments to a control method invocation (a call from one
 *              method to another.)
 *
 ****************************************************************************/

ACPI_STATUS
acpi_ds_resolve_operands (
	ACPI_WALK_STATE         *walk_state)
{
	u32                     i;
	ACPI_STATUS             status = AE_OK;


	/*
	 * Attempt to resolve each of the valid operands
	 * Method arguments are passed by value, not by reference
	 */

	/*
	 * TBD: [Investigate] Note from previous parser:
	 *   Ref_of problem with Acpi_aml_resolve_to_value() conversion.
	 */

	for (i = 0; i < walk_state->num_operands; i++) {
		status = acpi_aml_resolve_to_value (&walk_state->operands[i]);
		if (ACPI_FAILURE (status)) {
			break;
		}
	}

	return (status);
}


/*****************************************************************************
 *
 * FUNCTION:    Acpi_ds_map_opcode_to_data_type
 *
 * PARAMETERS:  Opcode          - AML opcode to map
 *              Out_flags       - Additional info about the opcode
 *
 * RETURN:      The ACPI type associated with the opcode
 *
 * DESCRIPTION: Convert a raw AML opcode to the associated ACPI data type,
 *              if any.  If the opcode returns a value as part of the
 *              intepreter execution, a flag is returned in Out_flags.
 *
 ****************************************************************************/

OBJECT_TYPE_INTERNAL
acpi_ds_map_opcode_to_data_type (
	u16                     opcode,
	u32                     *out_flags)
{
	OBJECT_TYPE_INTERNAL    data_type = INTERNAL_TYPE_INVALID;
	ACPI_OP_INFO            *op_info;
	u32                     flags = 0;


	op_info = acpi_ps_get_opcode_info (opcode);
	if (!op_info) {
		/* Unknown opcode */

		return data_type;
	}

	switch (op_info->flags & OP_INFO_TYPE)
	{

	case OPTYPE_LITERAL:

		switch (opcode)
		{
		case AML_BYTE_OP:
		case AML_WORD_OP:
		case AML_DWORD_OP:

			data_type = ACPI_TYPE_NUMBER;
			break;


		case AML_STRING_OP:

			data_type = ACPI_TYPE_STRING;
			break;

		case AML_NAMEPATH_OP:
			data_type = INTERNAL_TYPE_REFERENCE;
			break;
		}

		break;


	case OPTYPE_DATA_TERM:

		switch (opcode)
		{
		case AML_BUFFER_OP:

			data_type = ACPI_TYPE_BUFFER;
			break;

		case AML_PACKAGE_OP:

			data_type = ACPI_TYPE_PACKAGE;
			break;
		}

		break;


	case OPTYPE_CONSTANT:
	case OPTYPE_METHOD_ARGUMENT:
	case OPTYPE_LOCAL_VARIABLE:

		data_type = INTERNAL_TYPE_REFERENCE;
		break;


	case OPTYPE_MONADIC2:
	case OPTYPE_MONADIC2_r:
	case OPTYPE_DYADIC2:
	case OPTYPE_DYADIC2_r:
	case OPTYPE_DYADIC2_s:
	case OPTYPE_INDEX:
	case OPTYPE_MATCH:

		flags = OP_HAS_RETURN_VALUE;
		data_type = ACPI_TYPE_ANY;

		break;

	case OPTYPE_METHOD_CALL:

		flags = OP_HAS_RETURN_VALUE;
		data_type = ACPI_TYPE_METHOD;

		break;


	case OPTYPE_NAMED_OBJECT:

		data_type = acpi_ds_map_named_opcode_to_data_type (opcode);

		break;


	case OPTYPE_DYADIC1:
	case OPTYPE_CONTROL:

		/* No mapping needed at this time */

		break;


	default:

		break;
	}

	/* Return flags to caller if requested */

	if (out_flags) {
		*out_flags = flags;
	}

	return data_type;
}


/*****************************************************************************
 *
 * FUNCTION:    Acpi_ds_map_named_opcode_to_data_type
 *
 * PARAMETERS:  Opcode              - The Named AML opcode to map
 *
 * RETURN:      The ACPI type associated with the named opcode
 *
 * DESCRIPTION: Convert a raw Named AML opcode to the associated data type.
 *              Named opcodes are a subsystem of the AML opcodes.
 *
 ****************************************************************************/

OBJECT_TYPE_INTERNAL
acpi_ds_map_named_opcode_to_data_type (
	u16                     opcode)
{
	OBJECT_TYPE_INTERNAL    data_type;


	/* Decode Opcode */

	switch (opcode)
	{
	case AML_SCOPE_OP:
		data_type = INTERNAL_TYPE_SCOPE;
		break;

	case AML_DEVICE_OP:
		data_type = ACPI_TYPE_DEVICE;
		break;

	case AML_THERMAL_ZONE_OP:
		data_type = ACPI_TYPE_THERMAL;
		break;

	case AML_METHOD_OP:
		data_type = ACPI_TYPE_METHOD;
		break;

	case AML_POWER_RES_OP:
		data_type = ACPI_TYPE_POWER;
		break;

	case AML_PROCESSOR_OP:
		data_type = ACPI_TYPE_PROCESSOR;
		break;

	case AML_DEF_FIELD_OP:                          /* Def_field_op */
		data_type = INTERNAL_TYPE_DEF_FIELD_DEFN;
		break;

	case AML_INDEX_FIELD_OP:                        /* Index_field_op */
		data_type = INTERNAL_TYPE_INDEX_FIELD_DEFN;
		break;

	case AML_BANK_FIELD_OP:                         /* Bank_field_op */
		data_type = INTERNAL_TYPE_BANK_FIELD_DEFN;
		break;

	case AML_NAMEDFIELD_OP:                         /* NO CASE IN ORIGINAL  */
		data_type = ACPI_TYPE_ANY;
		break;

	case AML_NAME_OP:                               /* Name_op - special code in original */
	case AML_NAMEPATH_OP:
		data_type = ACPI_TYPE_ANY;
		break;

	case AML_ALIAS_OP:
		data_type = INTERNAL_TYPE_ALIAS;
		break;

	case AML_MUTEX_OP:
		data_type = ACPI_TYPE_MUTEX;
		break;

	case AML_EVENT_OP:
		data_type = ACPI_TYPE_EVENT;
		break;

	case AML_REGION_OP:
		data_type = ACPI_TYPE_REGION;
		break;


	default:
		data_type = ACPI_TYPE_ANY;
		break;

	}

	return data_type;
}

