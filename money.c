/*
  +----------------------------------------------------------------------+
  | PHP Version 5                                                        |
  +----------------------------------------------------------------------+
  | Copyright (c) 1997-2014 The PHP Group                                |
  +----------------------------------------------------------------------+
  | This source file is subject to version 3.01 of the PHP license,      |
  | that is bundled with this package in the file LICENSE, and is        |
  | available through the world-wide-web at the following url:           |
  | http://www.php.net/license/3_01.txt                                  |
  | If you did not receive a copy of the PHP license and are unable to   |
  | obtain it through the world-wide-web, please send a note to          |
  | license@php.net so we can mail you a copy immediately.               |
  +----------------------------------------------------------------------+
  | Author: Julien Pauli <jpauli@php.net>                                |
  +----------------------------------------------------------------------+
*/

/* $Id$ */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "php.h"
#include "php_ini.h"
#include "ext/standard/info.h"
#include "ext/spl/spl_exceptions.h"
#include "ext/standard/php_math.h"
#include "Zend/zend_exceptions.h"
#include "php_money.h"

#if ZEND_MODULE_API_NO < 20131226
#error "Please compile for PHP>=5.6"
#endif

static zend_class_entry *money_ce, *currency_ce, *CurrencyMismatchException_ce;
static zend_object_handlers money_object_handlers;

static const zend_function_entry money_functions[] = {
		PHP_ME(Money, __construct, arginfo_money__construct, ZEND_ACC_PUBLIC)
		PHP_ME(Money, getAmount,          0, ZEND_ACC_PUBLIC)
		PHP_ME(Money, getCurrency,        0, ZEND_ACC_PUBLIC)
		PHP_ME(Money, add,                arginfo_money_add, ZEND_ACC_PUBLIC)
		PHP_ME(Money, substract,          arginfo_money_add, ZEND_ACC_PUBLIC)
		PHP_ME(Money, negate,             0, ZEND_ACC_PUBLIC)
		PHP_ME(Money, multiply,           arginfo_money_multiply, ZEND_ACC_PUBLIC)
		PHP_ME(Money, compareTo,          arginfo_money_add, ZEND_ACC_PUBLIC)
		PHP_ME(Money, equals,             arginfo_money_add, ZEND_ACC_PUBLIC)
		PHP_ME(Money, greaterThan,        arginfo_money_add, ZEND_ACC_PUBLIC)
		PHP_ME(Money, greaterThanOrEqual, arginfo_money_add, ZEND_ACC_PUBLIC)
		PHP_ME(Money, lessThan,           arginfo_money_add, ZEND_ACC_PUBLIC)
		PHP_ME(Money, lessThanOrEqual,    arginfo_money_add, ZEND_ACC_PUBLIC)
		PHP_ME(Money, extractPercentage,  arginfo_money_extractPercentage, ZEND_ACC_PUBLIC)
		PHP_FE_END
};

static const zend_function_entry currency_functions[] = {
		PHP_ME(Currency, __construct, arginfo_currency__construct, ZEND_ACC_PUBLIC)
		PHP_FE_END
};

PHP_METHOD(Money, extractPercentage)
{
	long percentage;
	double result;
	zval *new_money_percentage, *amount, *new_money_subtotal;

	if (zend_parse_parameters(ZEND_NUM_ARGS(), "l", &percentage) == FAILURE) {
		return;
	}

	ALLOC_INIT_ZVAL(new_money_percentage);ALLOC_INIT_ZVAL(new_money_subtotal);

	amount = zend_read_property(money_ce, getThis(), MONEY_PROP_AMOUNT_WS, 0);
	result = Z_LVAL_P(amount) / (100 + percentage) * percentage;

	CREATE_NEW_MONEY_OBJ(new_money_percentage, zend_dval_to_lval(result), zend_read_property(money_ce, getThis(), MONEY_PROP_CURRENCY_WS, 0))

	array_init(return_value);
	add_assoc_zval(return_value, "percentage", new_money_percentage);

	money_handler_do_operation(ZEND_SUB, new_money_subtotal, getThis(), new_money_percentage);

	add_assoc_zval(return_value, "subtotal", new_money_subtotal);
}

PHP_METHOD(Money, lessThan)
{
	CHECK_MONEY_PARAMS

	RETURN_BOOL(money_handler_compare_objects(getThis(), other_money) == -1);
}

PHP_METHOD(Money, lessThanOrEqual)
{
	int result;

	CHECK_MONEY_PARAMS

	result = money_handler_compare_objects(getThis(), other_money);

	RETURN_BOOL(result == -1 || result == 0);
}

PHP_METHOD(Money, greaterThan)
{
	CHECK_MONEY_PARAMS

	RETURN_BOOL(money_handler_compare_objects(getThis(), other_money) == 1);
}

PHP_METHOD(Money, greaterThanOrEqual)
{
	int result;

	CHECK_MONEY_PARAMS

	result = money_handler_compare_objects(getThis(), other_money);

	RETURN_BOOL(result == 1 || result == 0);
}

PHP_METHOD(Money, equals)
{
	CHECK_MONEY_PARAMS

	RETURN_BOOL(money_handler_compare_objects(getThis(), other_money) == 0);
}

PHP_METHOD(Money, compareTo)
{
	CHECK_MONEY_PARAMS

	RETURN_LONG(money_handler_compare_objects(getThis(), other_money));
}

PHP_METHOD(Money, negate)
{
	zval sub_zval;

	CHECK_NO_PARAMS

	ZVAL_LONG(&sub_zval, 0);

	object_init_ex(return_value, money_ce);

	money_handler_do_operation(ZEND_SUB, return_value, &sub_zval, getThis());
}

PHP_METHOD(Money, add)
{
	CHECK_MONEY_PARAMS

	object_init_ex(return_value, money_ce);

	money_handler_do_operation(ZEND_ADD, return_value, getThis(), other_money);
}

PHP_METHOD(Money, substract)
{
	CHECK_MONEY_PARAMS

	object_init_ex(return_value, money_ce);

	money_handler_do_operation(ZEND_SUB, return_value, getThis(), other_money);
}

PHP_METHOD(Money, getAmount)
{
	CHECK_NO_PARAMS

	RETURN_ZVAL_FAST(zend_read_property(Z_OBJCE_P(getThis()), getThis(), MONEY_PROP_AMOUNT_WS, 0));
}

PHP_METHOD(Money, getCurrency)
{
	CHECK_NO_PARAMS

	RETURN_ZVAL_FAST(zend_read_property(Z_OBJCE_P(getThis()), getThis(), MONEY_PROP_CURRENCY_WS, 0));
}

PHP_METHOD(Money, multiply)
{
	double factor;
	volatile double dresult;
	long rounding_mode = PHP_ROUND_HALF_UP, lresult;

	if (zend_parse_parameters(ZEND_NUM_ARGS(), "d|l", &factor, &rounding_mode) == FAILURE) {
		return;
	}

	if (UNEXPECTED(rounding_mode < 0 || rounding_mode > PHP_ROUND_HALF_ODD)) {
		zend_throw_exception(spl_ce_InvalidArgumentException, "$roundingMode must be a valid rounding mode (PHP_ROUND_*)", 0);
		return;
	}

	dresult = _php_math_round(factor * Z_LVAL_P(zend_read_property(money_ce, getThis(), MONEY_PROP_AMOUNT_WS, 0)), 0, rounding_mode);
	lresult = zend_dval_to_lval(dresult);

	if (UNEXPECTED(lresult & LONG_SIGN_MASK)) {
		zend_throw_exception(spl_ce_OverflowException, "Integer overflow", 0);
		return;
	}

	CREATE_NEW_MONEY_OBJ(return_value, lresult, zend_read_property(money_ce, getThis(), MONEY_PROP_CURRENCY_WS, 0));
}

PHP_METHOD(Money, __construct)
{
	long amount;
	zval *currency, *currency_obj;

	if (zend_parse_parameters(ZEND_NUM_ARGS(), "lz", &amount, &currency) == FAILURE) {
		return;
	}

	switch(Z_TYPE_P(currency)) {
		case IS_OBJECT:
			if (!instanceof_function(currency_ce, Z_OBJCE_P(currency))) {
				zend_throw_exception(spl_ce_InvalidArgumentException, "Invalid currency object", 0);
				return;
			}
		break;
		case IS_STRING:
			ALLOC_INIT_ZVAL(currency_obj);
			object_init_ex(currency_obj, currency_ce);
			zend_update_property_stringl(currency_ce, currency_obj, CURRENCY_PROP_CURRENCYCODE_WS, Z_STRVAL_P(currency), Z_STRLEN_P(currency));
			currency = currency_obj;
			Z_DELREF_P(currency);
		break;
		default:
			zend_throw_exception(spl_ce_InvalidArgumentException, "Invalid currency value", 0);
			return;
	}

	zend_update_property_long(Z_OBJCE_P(getThis()), getThis(), MONEY_PROP_AMOUNT_WS, amount);
	zend_update_property(Z_OBJCE_P(getThis()), getThis(), MONEY_PROP_CURRENCY_WS, currency);
}

PHP_METHOD(Currency, __construct)
{
	char *currency_code;
	int currency_code_len;

	if (zend_parse_parameters(ZEND_NUM_ARGS(), "s", &currency_code, &currency_code_len) == FAILURE) {
		return;
	}

	zend_update_property_stringl(Z_OBJCE_P(getThis()), getThis(), CURRENCY_PROP_CURRENCYCODE_WS, currency_code, currency_code_len);
}

static int money_handler_compare_objects(zval *object1, zval *object2)
{
	zval *amount1, *amount2, *currency1, *currency2;
	long compare_result;

	currency1 = zend_read_property(Z_OBJCE_P(object1), object1, MONEY_PROP_CURRENCY_WS, 0);
	currency2 = zend_read_property(Z_OBJCE_P(object2), object2, MONEY_PROP_CURRENCY_WS, 0);

	if ((compare_result = Z_OBJ_HANDLER_P(currency1, compare_objects)(currency1, currency2)) != 0) {
		zend_throw_exception(CurrencyMismatchException_ce, "Currencies don't match", 0);
		return compare_result;
	}

	amount1 = zend_read_property(Z_OBJCE_P(object1), object1, MONEY_PROP_AMOUNT_WS, 0);
	amount2 = zend_read_property(Z_OBJCE_P(object2), object2, MONEY_PROP_AMOUNT_WS, 0);

	if (Z_LVAL_P(amount1) == Z_LVAL_P(amount2)) {
		return 0;
	} else if(Z_LVAL_P(amount1) < Z_LVAL_P(amount2)) {
		return -1;
	}

	return 1;
}

static int money_handler_do_operation(zend_uchar opcode, zval *result, zval *op1, zval *op2)
{
	zval *currency1 = NULL, *currency2 = NULL, *currency_result = NULL;

	long amount1, amount2, amount_result;

	switch (TYPE_PAIR(Z_TYPE_P(op1), Z_TYPE_P(op2))) {
		case TYPE_PAIR(IS_OBJECT, IS_OBJECT):
			if (!instanceof_function(Z_OBJCE_P(op1), money_ce) || !instanceof_function(Z_OBJCE_P(op2), money_ce)) {
				return FAILURE;
			}
			currency1 = zend_read_property(Z_OBJCE_P(op1), op1, MONEY_PROP_CURRENCY_WS, 0);
			currency2 = zend_read_property(Z_OBJCE_P(op2), op2, MONEY_PROP_CURRENCY_WS, 0);

			if (Z_OBJ_HANDLER_P(currency1, compare_objects)(currency1, currency2) != 0) {
				zend_throw_exception(CurrencyMismatchException_ce, "Currencies don't match", 0);
				ZVAL_NULL(result);
				return SUCCESS;
			}
			amount1         = Z_LVAL_P(zend_read_property(Z_OBJCE_P(op1), op1, MONEY_PROP_AMOUNT_WS, 0));
			amount2         = Z_LVAL_P(zend_read_property(Z_OBJCE_P(op2), op2, MONEY_PROP_AMOUNT_WS, 0));
			currency_result = currency1;
		break;

		case TYPE_PAIR(IS_LONG, IS_OBJECT): /* negate */
			if (!instanceof_function(Z_OBJCE_P(op2), money_ce)) {
				return FAILURE;
			}
			if (Z_LVAL_P(op1) != 0) {
				return FAILURE; /* I said negate */
			}
			amount1         = 0;
			amount2         = Z_LVAL_P(zend_read_property(Z_OBJCE_P(op2), op2, MONEY_PROP_AMOUNT_WS, 0));
			currency_result = zend_read_property(Z_OBJCE_P(op2), op2, MONEY_PROP_CURRENCY_WS, 0);
		break;
		default :
			return FAILURE;
	}

	INIT_ZVAL(*result);

	switch (opcode) {
		case ZEND_ADD:
			if (UNEXPECTED((amount1 & LONG_SIGN_MASK) == (amount2 & LONG_SIGN_MASK)
				   && (amount1 & LONG_SIGN_MASK) != ((amount1 + amount2) & LONG_SIGN_MASK))) {
					zend_throw_exception(spl_ce_OverflowException, "Integer overflow", 0);
					return FAILURE;
			}
			amount_result = amount1 + amount2;
			goto success;
		break;
		case ZEND_SUB:
			{
				amount_result = amount1 - amount2;
				if (amount_result == LONG_MIN) {
					zend_throw_exception(spl_ce_OverflowException, "Integer negative overflow", 0);
					return FAILURE;
				}
				goto success;
			}
		break;
		default:
			return FAILURE;
		break;
	}

success:
	CREATE_NEW_MONEY_OBJ(result, amount_result, currency_result);
	return SUCCESS;
}

static zend_object_value money_create_object(zend_class_entry *ce)
{
	zend_object_value retval;
	zend_object *obj;

	obj = emalloc(sizeof(zend_object));
	zend_object_std_init(obj, ce);
	object_properties_init(obj, ce);

	retval.handle   = zend_objects_store_put(obj, (zend_objects_store_dtor_t) zend_objects_destroy_object, (zend_objects_free_object_storage_t)zend_objects_free_object_storage, NULL);
	retval.handlers = &money_object_handlers;

	return retval;
}

PHP_MINIT_FUNCTION(money)
{
	zend_class_entry money_tmp, currency_tmp, CurrencyMismatchException_tmp;

	INIT_CLASS_ENTRY(money_tmp, "Money", money_functions);
	money_ce = zend_register_internal_class(&money_tmp);
	money_ce->create_object = money_create_object;
	zend_declare_property_long(money_ce, MONEY_PROP_AMOUNT, sizeof(MONEY_PROP_AMOUNT) -1, 0, ZEND_ACC_PRIVATE);
	zend_declare_property_long(money_ce, MONEY_PROP_CURRENCY, sizeof(MONEY_PROP_CURRENCY) -1, 0, ZEND_ACC_PRIVATE);
	memcpy(&money_object_handlers, &std_object_handlers, sizeof(std_object_handlers));
	money_object_handlers.do_operation    = money_handler_do_operation;
	money_object_handlers.compare_objects = money_handler_compare_objects;

	INIT_CLASS_ENTRY(currency_tmp, "Currency", currency_functions);
	currency_ce = zend_register_internal_class(&currency_tmp);
	currency_ce->ce_flags |= ZEND_ACC_FINAL_CLASS;
	zend_declare_property_stringl(currency_ce, CURRENCY_PROP_CURRENCYCODE_WS, ZEND_STRL(""), ZEND_ACC_PRIVATE);

	INIT_CLASS_ENTRY(CurrencyMismatchException_tmp, "CurrencyMismatchException", NULL);
	CurrencyMismatchException_ce = zend_register_internal_class_ex(&CurrencyMismatchException_tmp, spl_ce_InvalidArgumentException, NULL);

	return SUCCESS;
}

PHP_MINFO_FUNCTION(money)
{
	php_info_print_table_start();
	php_info_print_table_header(2, "money support", "enabled");
	php_info_print_table_colspan_header(2, "Based on https://github.com/sebastianbergmann/money");
	php_info_print_table_end();
}

zend_module_entry money_module_entry = {
	STANDARD_MODULE_HEADER,
	"money",
	NULL,
	PHP_MINIT(money),
	NULL,
	NULL,
	NULL,
	PHP_MINFO(money),
	PHP_MONEY_VERSION,
	STANDARD_MODULE_PROPERTIES
};

#ifdef COMPILE_DL_MONEY
ZEND_GET_MODULE(money)
#endif

/*
 * Local variables:
 * tab-width: 4
 * c-basic-offset: 4
 * End:
 * vim600: noet sw=4 ts=4 fdm=marker
 * vim<600: noet sw=4 ts=4
 */
