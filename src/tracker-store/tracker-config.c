/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2009, Nokia (urho.konttori@nokia.com)
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA  02110-1301, USA.
 */

#include "config.h"

#include <string.h>
#include <stdlib.h>

#include <glib.h>
#include <gio/gio.h>

#include <libtracker-common/tracker-config-utils.h>

#include "tracker-config.h"

#define TRACKER_CONFIG_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), TRACKER_TYPE_CONFIG, TrackerConfigPrivate))

/* GKeyFile defines */
#define GROUP_GENERAL		    "General"
#define GROUP_INDEXING		    "Indexing"

/* Default values */
#define DEFAULT_VERBOSITY	    2
#define DEFAULT_LOW_MEMORY_MODE	    FALSE
#define DEFAULT_MIN_WORD_LENGTH	    3	  /* 0->30 */
#define DEFAULT_MAX_WORD_LENGTH	    30	  /* 0->200 */
#define DEFAULT_MAX_WORDS_TO_INDEX  10000

/* typedef struct TrackerConfigPrivate TrackerConfigPrivate; */

typedef struct {
	/* General */
	gint     verbosity;
	gboolean low_memory_mode;

	/* Indexing */
	gint     min_word_length;
	gint     max_word_length;
	gint     max_words_to_index;
}  TrackerConfigPrivate;

typedef struct {
	GType  type;
	gchar *property;
	gchar *group;
	gchar *key;
} ObjectToKeyFile;

static void config_set_property         (GObject       *object,
					 guint          param_id,
					 const GValue  *value,
					 GParamSpec    *pspec);
static void config_get_property         (GObject       *object,
					 guint          param_id,
					 GValue        *value,
					 GParamSpec    *pspec);
static void config_finalize             (GObject       *object);
static void config_constructed          (GObject       *object);
static void config_create_with_defaults (TrackerConfig *config,
					 GKeyFile      *key_file, 
					 gboolean       overwrite);
static void config_load                 (TrackerConfig *config);

enum {
	PROP_0,

	/* General */
	PROP_VERBOSITY,
	PROP_LOW_MEMORY_MODE,

	/* Indexing */
	PROP_MIN_WORD_LENGTH,
	PROP_MAX_WORD_LENGTH,

	/* Performance */
	PROP_MAX_WORDS_TO_INDEX,
};

static ObjectToKeyFile conversions[] = {
	{ G_TYPE_INT,     "verbosity",          GROUP_GENERAL,  "Verbosity"       },
	{ G_TYPE_BOOLEAN, "low-memory-mode",    GROUP_GENERAL,  "LowMemoryMode"   },
	{ G_TYPE_INT,     "min-word-length",    GROUP_INDEXING, "MinWordLength"   },
	{ G_TYPE_INT,     "max-word-length",    GROUP_INDEXING, "MaxWordLength"   },
	{ G_TYPE_INT,     "max-words-to-index", GROUP_INDEXING, "MaxWordsToIndex" },
};

G_DEFINE_TYPE (TrackerConfig, tracker_config, TRACKER_TYPE_CONFIG_MANAGER);

static void
tracker_config_class_init (TrackerConfigClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->set_property = config_set_property;
	object_class->get_property = config_get_property;
	object_class->finalize	   = config_finalize;
	object_class->constructed  = config_constructed;

	/* General */
	g_object_class_install_property (object_class,
					 PROP_VERBOSITY,
					 g_param_spec_int ("verbosity",
							   "Log verbosity",
							   " Log verbosity (0=errors, 1=minimal, 2=detailed, 3=debug)",
							   0,
							   3,
							   DEFAULT_VERBOSITY,
							   G_PARAM_READWRITE | G_PARAM_CONSTRUCT));
	g_object_class_install_property (object_class,
					 PROP_LOW_MEMORY_MODE,
					 g_param_spec_boolean ("low-memory-mode",
							       "Use extra memory",
							       " Minimizes memory use at the expense of indexing speed",
							       DEFAULT_LOW_MEMORY_MODE,
							       G_PARAM_READWRITE | G_PARAM_CONSTRUCT));

	/* Indexing */
	g_object_class_install_property (object_class,
					 PROP_MIN_WORD_LENGTH,
					 g_param_spec_int ("min-word-length",
							   "Minimum word length",
							   " Set the minimum length of words to index (0->30, default=3)",
							   0,
							   30,
							   DEFAULT_MIN_WORD_LENGTH,
							   G_PARAM_READWRITE | G_PARAM_CONSTRUCT));
	g_object_class_install_property (object_class,
					 PROP_MAX_WORD_LENGTH,
					 g_param_spec_int ("max-word-length",
							   "Maximum word length",
							   " Set the maximum length of words to index (0->200, default=30)",
							   0,
							   200, /* Is this a reasonable limit? */
							   DEFAULT_MAX_WORD_LENGTH,
							   G_PARAM_READWRITE | G_PARAM_CONSTRUCT));
	g_object_class_install_property (object_class,
					 PROP_MAX_WORDS_TO_INDEX,
					 g_param_spec_int ("max-words-to-index",
							   "Maximum words to index",
							   " Maximum unique words to index from a file's content (default=10000)",
							   0,
							   G_MAXINT,
							   DEFAULT_MAX_WORDS_TO_INDEX,
							   G_PARAM_READWRITE | G_PARAM_CONSTRUCT));

	g_type_class_add_private (object_class, sizeof (TrackerConfigPrivate));
}

static void
tracker_config_init (TrackerConfig *object)
{
}

static void
config_set_property (GObject	  *object,
		     guint	   param_id,
		     const GValue *value,
		     GParamSpec	  *pspec)
{
	switch (param_id) {
		/* General */
	case PROP_VERBOSITY:
		tracker_config_set_verbosity (TRACKER_CONFIG (object),
					      g_value_get_int (value));
		break;
	case PROP_LOW_MEMORY_MODE:
		tracker_config_set_low_memory_mode (TRACKER_CONFIG (object),
						    g_value_get_boolean (value));
		break;

		/* Indexing */
	case PROP_MIN_WORD_LENGTH:
		tracker_config_set_min_word_length (TRACKER_CONFIG (object),
						    g_value_get_int (value));
		break;
	case PROP_MAX_WORD_LENGTH:
		tracker_config_set_max_word_length (TRACKER_CONFIG (object),
						    g_value_get_int (value));
		break;
	case PROP_MAX_WORDS_TO_INDEX:
		tracker_config_set_max_words_to_index (TRACKER_CONFIG (object),
						       g_value_get_int (value));
		break;

	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, param_id, pspec);
		break;
	};
}

static void
config_get_property (GObject	*object,
		     guint	 param_id,
		     GValue	*value,
		     GParamSpec *pspec)
{
	TrackerConfigPrivate *priv;

	priv = TRACKER_CONFIG_GET_PRIVATE (object);

	switch (param_id) {
		/* General */
	case PROP_VERBOSITY:
		g_value_set_int (value, priv->verbosity);
		break;
	case PROP_LOW_MEMORY_MODE:
		g_value_set_boolean (value, priv->low_memory_mode);
		break;

		/* Indexing */
	case PROP_MIN_WORD_LENGTH:
		g_value_set_int (value, priv->min_word_length);
		break;
	case PROP_MAX_WORD_LENGTH:
		g_value_set_int (value, priv->max_word_length);
		break;
	case PROP_MAX_WORDS_TO_INDEX:
		g_value_set_int (value, priv->max_words_to_index);
		break;

	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, param_id, pspec);
		break;
	};
}

static void
config_finalize (GObject *object)
{
	/* For now we do nothing here, we left this override in for
	 * future expansion.
	 */

	(G_OBJECT_CLASS (tracker_config_parent_class)->finalize) (object);
}

static void
config_constructed (GObject *object)
{
	(G_OBJECT_CLASS (tracker_config_parent_class)->constructed) (object);

	config_load (TRACKER_CONFIG (object));
}

static void
config_create_with_defaults (TrackerConfig *config,
			     GKeyFile      *key_file, 
			     gboolean       overwrite)
{
	gint i;

	g_message ("Loading defaults into GKeyFile...");
	
	for (i = 0; i < G_N_ELEMENTS (conversions); i++) {
		gboolean has_key;
		
		has_key = g_key_file_has_key (key_file, 
					      conversions[i].group, 
					      conversions[i].key, 
					      NULL);
		if (!overwrite && has_key) {
			continue;
		}
		
		switch (conversions[i].type) {
		case G_TYPE_INT:
			g_key_file_set_integer (key_file, 
						conversions[i].group, 
						conversions[i].key, 
						tracker_config_default_int (config, 
									    conversions[i].property));
			break;

		case G_TYPE_BOOLEAN:
			g_key_file_set_boolean (key_file, 
						conversions[i].group, 
						conversions[i].key, 
						tracker_config_default_boolean (config, 
										conversions[i].property));
			break;

		default:
			g_assert_not_reached ();
			break;
		}

		g_key_file_set_comment (key_file, 
					conversions[i].group, 
					conversions[i].key, 
					tracker_config_blurb (config,
							      conversions[i].property), 
					NULL);
	}
}

static void
config_load (TrackerConfig *config)
{
	TrackerConfigManager *manager;
	gint i;

	manager = TRACKER_CONFIG_MANAGER (config);
	config_create_with_defaults (config, manager->key_file, FALSE);

	if (!manager->file_exists) {
		tracker_config_manager_save (manager);
	}

	for (i = 0; i < G_N_ELEMENTS (conversions); i++) {
		gboolean has_key;
		
		has_key = g_key_file_has_key (manager->key_file, 
					      conversions[i].group, 
					      conversions[i].key, 
					      NULL);
	
		switch (conversions[i].type) {
		case G_TYPE_INT:
			tracker_config_load_int (G_OBJECT (manager), 
						 conversions[i].property,
						 manager->key_file,
						 conversions[i].group, 
						 conversions[i].key);
			break;

		case G_TYPE_BOOLEAN:
			tracker_config_load_boolean (G_OBJECT (manager), 
						     conversions[i].property,
						     manager->key_file,
						     conversions[i].group, 
						     conversions[i].key);
			break;

		default:
			g_assert_not_reached ();
			break;
		}
	}
}

static gboolean
config_save (TrackerConfig *config)
{
	TrackerConfigManager *manager;
	gint i;

	manager = TRACKER_CONFIG_MANAGER (config);

	if (!manager->key_file) {
		g_critical ("Could not save config, GKeyFile was NULL, has the config been loaded?");

		return FALSE;
	}

	g_message ("Setting details to GKeyFile object...");

	for (i = 0; i < G_N_ELEMENTS (conversions); i++) {
		switch (conversions[i].type) {
		case G_TYPE_INT:
			tracker_config_save_int (manager,
						 conversions[i].property, 
						 manager->key_file,
						 conversions[i].group, 
						 conversions[i].key);
			break;

		case G_TYPE_BOOLEAN:
			tracker_config_save_boolean (manager,
						     conversions[i].property, 
						     manager->key_file,
						     conversions[i].group, 
						     conversions[i].key);
			break;

		default:
			g_assert_not_reached ();
			break;
		}
	}

	return tracker_config_manager_save (TRACKER_CONFIG_MANAGER (config));
}

TrackerConfig *
tracker_config_new (void)
{
	return g_object_new (TRACKER_TYPE_CONFIG, NULL);
}

gboolean
tracker_config_save (TrackerConfig *config)
{
	g_return_val_if_fail (TRACKER_IS_CONFIG (config), FALSE);

	return config_save (config);
}

gint
tracker_config_get_verbosity (TrackerConfig *config)
{
	TrackerConfigPrivate *priv;

	g_return_val_if_fail (TRACKER_IS_CONFIG (config), DEFAULT_VERBOSITY);

	priv = TRACKER_CONFIG_GET_PRIVATE (config);

	return priv->verbosity;
}

gboolean
tracker_config_get_low_memory_mode (TrackerConfig *config)
{
	TrackerConfigPrivate *priv;

	g_return_val_if_fail (TRACKER_IS_CONFIG (config), DEFAULT_LOW_MEMORY_MODE);

	priv = TRACKER_CONFIG_GET_PRIVATE (config);

	return priv->low_memory_mode;
}


gint
tracker_config_get_min_word_length (TrackerConfig *config)
{
	TrackerConfigPrivate *priv;

	g_return_val_if_fail (TRACKER_IS_CONFIG (config), DEFAULT_MIN_WORD_LENGTH);

	priv = TRACKER_CONFIG_GET_PRIVATE (config);

	return priv->min_word_length;
}

gint
tracker_config_get_max_word_length (TrackerConfig *config)
{
	TrackerConfigPrivate *priv;

	g_return_val_if_fail (TRACKER_IS_CONFIG (config), DEFAULT_MAX_WORD_LENGTH);

	priv = TRACKER_CONFIG_GET_PRIVATE (config);

	return priv->max_word_length;
}

gint
tracker_config_get_max_words_to_index (TrackerConfig *config)
{
	TrackerConfigPrivate *priv;

	g_return_val_if_fail (TRACKER_IS_CONFIG (config), DEFAULT_MAX_WORDS_TO_INDEX);

	priv = TRACKER_CONFIG_GET_PRIVATE (config);

	return priv->max_words_to_index;
}

void
tracker_config_set_verbosity (TrackerConfig *config,
			      gint	     value)
{
	TrackerConfigPrivate *priv;

	g_return_if_fail (TRACKER_IS_CONFIG (config));

	if (!tracker_config_validate_int (config, "verbosity", value)) {
		return;
	}

	priv = TRACKER_CONFIG_GET_PRIVATE (config);

	priv->verbosity = value;
	g_object_notify (G_OBJECT (config), "verbosity");
}

void
tracker_config_set_low_memory_mode (TrackerConfig *config,
				    gboolean	   value)
{
	TrackerConfigPrivate *priv;

	g_return_if_fail (TRACKER_IS_CONFIG (config));

	priv = TRACKER_CONFIG_GET_PRIVATE (config);

	priv->low_memory_mode = value;
	g_object_notify (G_OBJECT (config), "low-memory-mode");
}

void
tracker_config_set_min_word_length (TrackerConfig *config,
				    gint	   value)
{
	TrackerConfigPrivate *priv;

	g_return_if_fail (TRACKER_IS_CONFIG (config));

	if (!tracker_config_validate_int (config, "min-word-length", value)) {
		return;
	}

	priv = TRACKER_CONFIG_GET_PRIVATE (config);

	priv->min_word_length = value;
	g_object_notify (G_OBJECT (config), "min-word-length");
}

void
tracker_config_set_max_word_length (TrackerConfig *config,
				    gint	   value)
{
	TrackerConfigPrivate *priv;

	g_return_if_fail (TRACKER_IS_CONFIG (config));

	if (!tracker_config_validate_int (config, "max-word-length", value)) {
		return;
	}

	priv = TRACKER_CONFIG_GET_PRIVATE (config);

	priv->max_word_length = value;
	g_object_notify (G_OBJECT (config), "max-word-length");
}

void
tracker_config_set_max_words_to_index (TrackerConfig *config,
				       gint	      value)
{
	TrackerConfigPrivate *priv;

	g_return_if_fail (TRACKER_IS_CONFIG (config));

	if (!tracker_config_validate_int (config, "max-words-to-index", value)) {
		return;
	}

	priv = TRACKER_CONFIG_GET_PRIVATE (config);

	priv->max_words_to_index = value;
	g_object_notify (G_OBJECT (config), "max-words-to-index");
}
