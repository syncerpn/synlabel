#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <assert.h>
//#include <unistd.h>
#include <float.h>
#include <limits.h>
#include <time.h>
//#include <sys/time.h>
#include "SFML/Graphics.hpp"
#include "SFML/Window.hpp"

using namespace std;
using namespace sf;

typedef enum {
	DRAW_STATE_BEGIN = -1, CLEAR = 0, ONE_POINT, BOX_READY, DRAW_STATE_END
} DRAW_STATE;

static int N_DRAW_STATE = DRAW_STATE_END - DRAW_STATE_BEGIN - 1;

typedef struct {
	float l;
	float t;
	float r;
	float b;
	int class_id;
	int difficult;
	int highlight;
} gt_box;

//DARKNET API
typedef struct node {
	void *val;
	struct node *next;
	struct node *prev;
} node;

typedef struct list {
	int size;
	node *front;
	node *back;
} list;

void malloc_error()
{
	fprintf(stderr, "Malloc error\n");
	exit(-1);
}

void free_node(node *n)
{
	node *next;
	while (n) {
		next = n->next;
		free(n);
		n = next;
	}
}

void free_list(list *l)
{
	free_node(l->front);
	free(l);
}

void **list_to_array(list *l)
{
	void **a = (void**)calloc(l->size, sizeof(void*));
	int count = 0;
	node *n = l->front;
	while (n) {
		a[count++] = n->val;
		n = n->next;
	}
	return a;
}


void list_insert(list *l, void *val)
{
	node *newn = (node*)malloc(sizeof(node));
	newn->val = val;
	newn->next = 0;

	if (!l->back) {
		l->front = newn;
		newn->prev = 0;
	}
	else {
		l->back->next = newn;
		newn->prev = l->back;
	}
	l->back = newn;
	++l->size;
}

char *fgetl(FILE *fp)
{
	if (feof(fp)) return 0;
	size_t size = 512;
	char *line = (char*)malloc(size * sizeof(char));
	if (!fgets(line, size, fp)) {
		free(line);
		return 0;
	}

	size_t curr = strlen(line);

	while ((line[curr - 1] != '\n') && !feof(fp)) {
		if (curr == size - 1) {
			size *= 2;
			line = (char*)realloc(line, size * sizeof(char));
			if (!line) {
				printf("%ld\n", size);
				malloc_error();
			}
		}
		size_t readsize = size - curr;
		if (readsize > INT_MAX) readsize = INT_MAX - 1;
		fgets(&line[curr], readsize, fp);
		curr = strlen(line);
	}
	if (line[curr - 1] == '\n') line[curr - 1] = '\0';

	return line;
}

void file_error(char *s)
{
	fprintf(stderr, "Couldn't open file: %s\n", s);
	exit(0);
}

list *make_list()
{
	list *l = (list*)malloc(sizeof(list));
	l->size = 0;
	l->front = 0;
	l->back = 0;
	return l;
}

list *get_paths(char *filename)
{
	char *path;
	FILE *file = fopen(filename, "r");
	if (!file) file_error(filename);
	list *lines = make_list();
	while ((path = fgetl(file))) {
		list_insert(lines, path);
	}
	fclose(file);
	return lines;
}

extern "C" void find_replace(char *str, char *orig, char *rep, char *output)
{
	char buffer[4096] = { 0 };
	char *p;

	sprintf(buffer, "%s", str);
	if (!(p = strstr(buffer, orig))) {
		sprintf(output, "%s", str);
		return;
	}

	*p = '\0';

	sprintf(output, "%s%s%s", buffer, rep, p + strlen(orig));
}

float colors[6][3] = { { 1,0,1 },{ 0,0,1 },{ 0,1,1 },{ 0,1,0 },{ 1,1,0 },{ 1,0,0 } };

float get_color(int c, int x, int max)
{
	float ratio = ((float)x / max) * 5;
	int i = floor(ratio);
	int j = ceil(ratio);
	ratio -= i;
	float r = (1 - ratio) * colors[i][c] + ratio*colors[j][c];
	return r;
}

char **get_labels_with_n(char *filename, int* n)
{
	list *plist = get_paths(filename);
	char **labels = (char **)list_to_array(plist);
	*n = plist->size;
	free_list(plist);
	return labels;
}
//DARKNET API

sf::Vector2f get_mouse_pressed_pos(sf::Event event) {
	return sf::Vector2f(event.mouseButton.x, event.mouseButton.y);
}

sf::Vector2f get_mouse_move_pos(sf::Event event) {
	return sf::Vector2f(event.mouseMove.x, event.mouseMove.y);
}

float get_mouse_move_pos_x(sf::Event event) {
	return event.mouseMove.x;
}

float get_mouse_move_pos_y(sf::Event event) {
	return event.mouseMove.y;
}

void set_prev_highlight(gt_box* gt_boxes, int n_box, int& current_highlight) {
	int i;
	if (current_highlight == -1) {
		i = n_box - 1;
	}
	else {
		i = current_highlight - 1;
	}
	while (i != current_highlight) {
		if (i < 0) {
			if (current_highlight == n_box - 1) break;
			i = n_box - 1;
		}
		if (gt_boxes[i].class_id != -1) {
			gt_boxes[i].highlight = 1;
			current_highlight = i;
			return;
		}
		--i;
	}
	current_highlight = -1;
}

void set_next_highlight(gt_box* gt_boxes, int n_box, int& current_highlight) {
	int i;
	if (current_highlight == -1) {
		current_highlight = n_box;
		i = 0;
	}
	else {
		i = current_highlight + 1;
	}
	while (i != current_highlight) {
		if (i > n_box - 1) {
			if (current_highlight == 0) break;
			i = 0;
		}
		if (gt_boxes[i].class_id != -1) {
			gt_boxes[i].highlight = 1;
			current_highlight = i;
			return;
		}
		++i;
	}
	current_highlight = -1;
}

void correct_box(gt_box* box, int im_w, int im_h) {
	if (box->l > box->r) {
		float tmp = box->l;
		box->l = box->r;
		box->r = tmp;
	}

	if (box->t > box->b) {
		float tmp = box->t;
		box->t = box->b;
		box->b = tmp;
	}

	if (box->l < 0) box->l = 0;
	if (box->r > im_w - 1) box->r = im_w - 1;
	if (box->t < 0) box->t = 0;
	if (box->b > im_h - 1) box->b = im_h - 1;
}

void render_preview_box(sf::RenderWindow* window, gt_box box, int n_class) {
	int offset = box.class_id * 123457 % n_class;
	int r = (int)(get_color(2, offset, n_class) * 255);
	int g = (int)(get_color(1, offset, n_class) * 255);
	int b = (int)(get_color(0, offset, n_class) * 255);

	sf::Color color;
	sf::RectangleShape rbox;

	color = sf::Color(r, g, b);

	rbox.setSize(sf::Vector2f(box.r - box.l, box.b - box.t));
	rbox.setOutlineThickness(2);
	rbox.setOutlineColor(color);
	rbox.setPosition(box.l, box.t);
	if (box.difficult) rbox.setFillColor(sf::Color(255, 0, 0, 128));
	else rbox.setFillColor(sf::Color(0, 0, 0, 0));

	window->draw(rbox);
}

void render_box(sf::RenderWindow* window, gt_box box, int n_class) {
	int offset = box.class_id * 123457 % n_class;
	int r = (int)(get_color(2, offset, n_class) * 255);
	int g = (int)(get_color(1, offset, n_class) * 255);
	int b = (int)(get_color(0, offset, n_class) * 255);

	sf::Color color;
	sf::RectangleShape rbox;

	if (box.highlight) {
		color = sf::Color(r, g, b);
		if (box.difficult) rbox.setFillColor(sf::Color(255, 0, 0, 128));
		else rbox.setFillColor(sf::Color(0, 0, 0, 0));
	}
	else {
		color = sf::Color(255, 255, 255);
		rbox.setFillColor(sf::Color(0, 0, 0, 0));
	}

	rbox.setSize(sf::Vector2f(box.r - box.l, box.b - box.t));
	rbox.setOutlineThickness(2);
	rbox.setOutlineColor(color);
	rbox.setPosition(box.l, box.t);

	window->draw(rbox);
}

//new method +===================================+===================================+===================================+===================================+===================================+===================================
//please add core for detection
void do_box_labeling_compact(char* image_name, char** class_list, int n_class) {
	float scale_factor = 0.8;
	int i;

	sf::Texture image;

	image.loadFromFile(image_name);
	int im_w_ori = image.getSize().x;
	int im_h_ori = image.getSize().y;

	int im_w = im_w_ori * scale_factor;
	fprintf(stderr, "%d\n", im_w);
	int im_h = im_h_ori * scale_factor;
	fprintf(stderr, "%d\n", im_h);

	char label_name[256];
	char label_diff_name[256];
	sprintf(label_name, "%s", image_name);
	find_replace(label_name, ".jpg", ".txt", label_name);
	find_replace(label_name, ".png", ".txt", label_name);
	find_replace(label_name, "images", "labels", label_name);
	find_replace(label_name, "JPEGImages", "labels", label_name);

	sprintf(label_diff_name, "%s", label_name);
	find_replace(label_diff_name, "labels", "labels_diff", label_diff_name);

	gt_box* preload_gt_boxes = 0;
	int preload_n_box = 0;

	FILE* label_file;
	label_file = fopen(label_name, "r");

	FILE* label_diff_file;
	label_diff_file = fopen(label_diff_name, "r");

	if (label_file != NULL) {
		float x, y, h, w;
		int id;
		int diff;
		int count = 0;
		int size = 64;
		preload_gt_boxes = (gt_box*)calloc(size, sizeof(gt_box));
		while (fscanf(label_file, "%d %f %f %f %f", &id, &x, &y, &w, &h) == 5) {
			if (count == size) {
				size = size * 2;
				preload_gt_boxes = (gt_box*)realloc(preload_gt_boxes, size * sizeof(gt_box));
			}
			if (fscanf(label_diff_file, "%d", &diff) != 1) diff = 0;

			preload_gt_boxes[count].class_id = id;
			preload_gt_boxes[count].difficult = diff;
			preload_gt_boxes[count].l = (x - w / 2) * im_w + 0.5;
			preload_gt_boxes[count].r = (x + w / 2) * im_w - 0.5;
			preload_gt_boxes[count].t = (y - h / 2) * im_h + 0.5;
			preload_gt_boxes[count].b = (y + h / 2) * im_h - 0.5;
			++count;
		}
		preload_n_box = count;
		correct_box(preload_gt_boxes, im_w, im_h);
		fclose(label_file);
	}
	int n_box;
	int full_gt_stack = 0;
	int window_state = 0; //work state, 1 = show_state

	gt_box* gt_boxes;
	if (preload_gt_boxes) {
		n_box = preload_n_box;
		gt_boxes = (gt_box*)calloc(n_box, sizeof(gt_box));
		memcpy(gt_boxes, preload_gt_boxes, n_box * sizeof(gt_box));
		window_state = 1;
	}
	else {
		n_box = 4;
		gt_boxes = (gt_box*)calloc(n_box, sizeof(gt_box));
		for (i = 0; i < n_box; ++i) {
			gt_boxes[i].class_id = -1;
		}
		window_state = 0;
	}

	int current_cls = 0;
	int current_highlight = -1;
	int preview_highlight = -1;
	int current_difficult = 0;

	sf::Font font;
	font.loadFromFile("Roboto-Regular.ttf");

	//class list window
	int class_name_w = 100;
	int class_name_h = 20;

	char class_names_buf[256];
	sf::Text class_names;
	class_names.setFont(font);
	class_names.setCharacterSize(class_name_h - 4);
	class_names.setFillColor(sf::Color(255, 80, 80));

	//main window
	int border_w = 20;
	sf::RenderWindow show_window(sf::VideoMode(im_w + border_w + class_name_w, im_h + 20), "show", sf::Style::Close);
	show_window.setFramerateLimit(60);
	show_window.setPosition(sf::Vector2i(0, 0));

	sf::RectangleShape border;
	border.setSize(sf::Vector2f(border_w, im_h + 20));
	border.setFillColor(sf::Color(40, 40, 40, 255));
	border.setPosition(im_w, 0);

	char box_info_buf[256];

	sf::Text info;
	info.setFont(font);
	info.setString("info");
	info.setCharacterSize(16);
	info.setFillColor(sf::Color(255, 80, 80));
	info.setPosition(10, im_h);

	sf::Sprite spr;
	spr.setTexture(image);
	spr.setScale(sf::Vector2f(scale_factor, scale_factor));
	spr.setPosition(0, 0);

	sf::Vector2f vector_zero = sf::Vector2f(0, 0);
	sf::Vertex cursor_h_line[2] = { vector_zero, vector_zero };
	sf::Vertex cursor_v_line[2] = { vector_zero, vector_zero };

	sf::Vertex box_12[2] = { vector_zero, vector_zero };
	sf::Vertex box_13[2] = { vector_zero, vector_zero };
	sf::Vertex box_42[2] = { vector_zero, vector_zero };
	sf::Vertex box_43[2] = { vector_zero, vector_zero };

	sf::Vector2f box_1 = vector_zero;
	sf::Vector2f box_2 = vector_zero;
	sf::Vector2f box_3 = vector_zero;
	sf::Vector2f box_4 = vector_zero;

	int draw_state = CLEAR;
	int box_edit_mode = 5;
	float saved_l = 0;
	float saved_r = 0;
	float saved_t = 0;
	float saved_b = 0;
	//mask buffer
	sf::RectangleShape mask;
	mask.setFillColor(sf::Color(0, 0, 0, 184));

	sf::RectangleShape class_mask;
	class_mask.setPosition(class_name_w, -1 * class_name_h);
	class_mask.setFillColor(sf::Color(128, 128, 240, 100));
	class_mask.setSize(sf::Vector2f(class_name_w, class_name_h));

	sf::RectangleShape selected_mask;
	selected_mask.setFillColor(sf::Color(240, 160, 160, 100));
	selected_mask.setSize(sf::Vector2f(class_name_w, class_name_h));

	while (show_window.isOpen()) {
		Event event;
		while (show_window.pollEvent(event)) {

			if (event.type == sf::Event::Closed) {
				show_window.close();
			}

			if (window_state == 0) { //work state
				if (event.type == sf::Event::MouseMoved) {
					if (event.mouseMove.x > im_w + border_w) {
						int selected = (int)(event.mouseMove.y / class_name_h);
						if ((selected < 0) || (selected > n_class - 1)) selected = -1;
						else class_mask.setPosition(im_w + border_w, selected * class_name_h);
					}
					else {
						cursor_h_line[0] = sf::Vertex(sf::Vector2f(0, event.mouseMove.y));
						cursor_h_line[1] = sf::Vertex(sf::Vector2f(im_w, event.mouseMove.y));
						cursor_v_line[0] = sf::Vertex(sf::Vector2f(event.mouseMove.x, 0));
						cursor_v_line[1] = sf::Vertex(sf::Vector2f(event.mouseMove.x, im_h));
						if (draw_state == ONE_POINT) {
							box_2.x = get_mouse_move_pos_x(event);
							box_3.y = get_mouse_move_pos_y(event);
							box_4 = get_mouse_move_pos(event);
						}
					}
				}
				if (event.type == sf::Event::MouseButtonPressed) {
					if (event.mouseButton.x > im_w + border_w) {
						int selected = (int)(event.mouseButton.y / class_name_h);
						if ((selected < 0) || (selected > n_class - 1)) selected = -1;
						else current_cls = selected;
					}
					else {
						if (event.mouseButton.button == sf::Mouse::Left) {
							draw_state = (draw_state + 1) % N_DRAW_STATE;
							if (draw_state == ONE_POINT) {
								box_1 = get_mouse_pressed_pos(event);
								box_2 = box_1;
								box_3 = box_1;
								box_4 = box_1;
							}
							else if (draw_state == CLEAR) {
								box_1 = vector_zero;
								box_2 = box_1;
								box_3 = box_1;
								box_4 = box_1;
							}
						}
						else if (event.mouseButton.button == sf::Mouse::Right) {
							draw_state = CLEAR;
							box_1 = vector_zero;
							box_2 = box_1;
							box_3 = box_1;
							box_4 = box_1;
						}
					}
				}
				if (event.type == sf::Event::MouseWheelMoved) {
					if (event.mouseWheel.delta > 0) {
						current_cls = (current_cls + event.mouseWheel.delta) % n_class;
					}
					else if (event.mouseWheel.delta < 0) {
						current_cls = (current_cls + event.mouseWheel.delta) % n_class;
						current_cls = current_cls < 0 ? current_cls + n_class : current_cls;
					}
				}
				// keypressed event
				if (event.type == sf::Event::KeyPressed) {
					if (event.key.code == sf::Keyboard::Tab) {
						window_state = 1 - window_state;
					}
					if (event.key.code == sf::Keyboard::Escape) {
						show_window.close();
					}
					if (event.key.code == sf::Keyboard::C) {
						draw_state = CLEAR;
						box_1 = vector_zero;
						box_2 = box_1;
						box_3 = box_1;
						box_4 = box_1;
					}
					if (event.key.code == sf::Keyboard::Enter || event.key.code == sf::Keyboard::Space) {
						if (draw_state == BOX_READY) {
							float max_x = box_1.x > box_4.x ? box_1.x : box_4.x;
							float min_x = box_1.x < box_4.x ? box_1.x : box_4.x;
							float max_y = box_1.y > box_4.y ? box_1.y : box_4.y;
							float min_y = box_1.y < box_4.y ? box_1.y : box_4.y;
							float l = min_x;
							float t = min_y;
							float r = max_x;
							float b = max_y;
							//save box to gt_boxes
							if (current_highlight != -1) gt_boxes[current_highlight].highlight = 0;
							for (i = 0; i < n_box; ++i) {
								full_gt_stack = 1;
								if (gt_boxes[i].class_id == -1) {
									gt_boxes[i].l = l;
									gt_boxes[i].t = t;
									gt_boxes[i].r = r;
									gt_boxes[i].b = b;
									gt_boxes[i].class_id = current_cls;
									gt_boxes[i].difficult = current_difficult;
									gt_boxes[i].highlight = 1;

									current_highlight = i;
									full_gt_stack = 0;
									break;
								}
							}
							if (full_gt_stack) {
								n_box *= 2;
								gt_boxes = (gt_box*)realloc(gt_boxes, n_box * sizeof(gt_box));
								gt_boxes[i].l = l;
								gt_boxes[i].t = t;
								gt_boxes[i].r = r;
								gt_boxes[i].b = b;
								gt_boxes[i].class_id = current_cls;
								gt_boxes[i].difficult = current_difficult;
								gt_boxes[i].highlight = 1;

								current_highlight = i;
								full_gt_stack = 0;
								for (++i; i < n_box; ++i) {
									gt_boxes[i].class_id = -1;
									gt_boxes[i].highlight = 0;
								}
							}
							//clear all
							draw_state = CLEAR;
							box_1 = sf::Vector2f(0, 0);
							box_2 = box_1;
							box_3 = box_1;
							box_4 = box_1;
						}
					}
					if (event.key.code == sf::Keyboard::Up) {
						current_cls = (current_cls + 1) % n_class;
					}
					if (event.key.code == sf::Keyboard::Down) {
						current_cls = (current_cls - 1) % n_class;
						current_cls = current_cls < 0 ? current_cls + n_class : current_cls;
					}
					if (event.key.code == sf::Keyboard::Tilde) {
						current_difficult = 1 - current_difficult;
					}
					if (event.key.code == sf::Keyboard::S) {
						FILE* file = fopen(label_name, "w");
						FILE* file_diff = fopen(label_diff_name, "w");
						for (i = 0; i < n_box; ++i) {
							if (gt_boxes[i].class_id != -1) {
								float x = (gt_boxes[i].l + gt_boxes[i].r) / (2 * im_w);
								float y = (gt_boxes[i].t + gt_boxes[i].b) / (2 * im_h);
								float w = (gt_boxes[i].r - gt_boxes[i].l + 1) / im_w;
								float h = (gt_boxes[i].b - gt_boxes[i].t + 1) / im_h;
								fprintf(file, "%d %.16f %.16f %.16f %.16f\n", gt_boxes[i].class_id, x, y, w, h);
								fprintf(file_diff, "%d\n", gt_boxes[i].difficult);
							}
						}
						fclose(file);
						fclose(file_diff);
						fprintf(stderr, "Saved\n");
					}
				}
				selected_mask.setPosition(im_w + border_w, current_cls * class_name_h);
			}
			else { //show state
				if (current_highlight == -1) { //preview mode
					if (event.type == sf::Event::MouseMoved) {
						if (event.mouseMove.x > im_w + border_w) {
							int selected = (int)(event.mouseMove.y / class_name_h);
							if ((selected < 0) || (selected > n_class - 1)) selected = -1;
							else class_mask.setPosition(im_w + border_w, selected * class_name_h);
						}
						else {
							float prev_x = event.mouseMove.x;
							float prev_y = event.mouseMove.y;
							float min_dist = INFINITY;
							preview_highlight = -1;

							for (i = 0; i < n_box; ++i) {
								if (gt_boxes[i].class_id != -1) {
									if ((prev_x >= gt_boxes[i].l) && (prev_y >= gt_boxes[i].t) && (prev_x <= gt_boxes[i].r) && (prev_y <= gt_boxes[i].b)) {
										float dist = fabsf(prev_x - gt_boxes[i].l) + fabsf(prev_y - gt_boxes[i].t);
										if (dist < min_dist) {
											preview_highlight = i;
										}
									}
								}
							}
						}
					}
					if (event.type == sf::Event::MouseButtonPressed) {
						if (event.mouseButton.x > im_w + border_w) {
							if (event.mouseButton.button == sf::Mouse::Left) {
								int selected = (int)(event.mouseButton.y / class_name_h);
								if ((selected < 0) || (selected > n_class - 1)) selected = -1;
								else current_cls = selected;
							}
						}
						else {
							if (event.mouseButton.button == sf::Mouse::Left) {
								if (preview_highlight != -1) {
									current_highlight = preview_highlight;
									preview_highlight = -1;
									gt_boxes[current_highlight].highlight = 1;
									box_edit_mode = 5;
								}
							}
						}
					}
					if (event.type == sf::Event::KeyPressed) {
						if (event.key.code == sf::Keyboard::Left) {
							box_edit_mode = 5;
							set_prev_highlight(gt_boxes, n_box, current_highlight);
						}
						if (event.key.code == sf::Keyboard::Right) {
							box_edit_mode = 5;
							set_next_highlight(gt_boxes, n_box, current_highlight);
						}
						if (event.key.code == sf::Keyboard::Escape) {
							show_window.close();
						}
						if (event.key.code == sf::Keyboard::Delete) {
							if (preview_highlight != -1) {
								gt_boxes[preview_highlight].class_id = -1;
								gt_boxes[preview_highlight].highlight = 0;
								preview_highlight = -1;
							}
						}
						if (event.key.code == sf::Keyboard::Tilde) {
							if (preview_highlight != -1) gt_boxes[preview_highlight].difficult = 1 - gt_boxes[preview_highlight].difficult;
							else current_difficult = 1 - current_difficult;
						}
						if (event.key.code == sf::Keyboard::R) {
							if (preload_gt_boxes) {
								free(gt_boxes);
								n_box = preload_n_box;
								fprintf(stderr, "Reloaded\n");
								gt_boxes = (gt_box*)calloc(n_box, sizeof(gt_box));
								memcpy(gt_boxes, preload_gt_boxes, n_box * sizeof(gt_box));
							}
						}
						if (event.key.code == sf::Keyboard::Tab) {
							window_state = 1 - window_state;
						}
						if (event.key.code == sf::Keyboard::S) {
							FILE* file = fopen(label_name, "w");
							FILE* file_diff = fopen(label_diff_name, "w");
							for (i = 0; i < n_box; ++i) {
								if (gt_boxes[i].class_id != -1) {
									float x = (gt_boxes[i].l + gt_boxes[i].r) / (2 * im_w);
									float y = (gt_boxes[i].t + gt_boxes[i].b) / (2 * im_h);
									float w = (gt_boxes[i].r - gt_boxes[i].l + 1) / im_w;
									float h = (gt_boxes[i].b - gt_boxes[i].t + 1) / im_h;
									fprintf(file, "%d %.16f %.16f %.16f %.16f\n", gt_boxes[i].class_id, x, y, w, h);
									fprintf(file_diff, "%d\n", gt_boxes[i].difficult);
								}
							}
							fclose(file);
							fclose(file_diff);
							fprintf(stderr, "Saved\n");
						}
					}
					if (preview_highlight != -1) {
						selected_mask.setPosition(im_w + border_w, gt_boxes[preview_highlight].class_id * class_name_h);
					}
					else {
						selected_mask.setPosition(im_w + border_w, current_cls * class_name_h);
					}
				}
				else { //highlight
					preview_highlight = -1;
					if (event.type == sf::Event::MouseMoved) {
						if (event.mouseMove.x > im_w + border_w) {
							int selected = (int)(event.mouseMove.y / class_name_h);
							if ((selected < 0) || (selected > n_class - 1)) selected = -1;
							else class_mask.setPosition(im_w + border_w, selected * class_name_h);
						}
						else {
							if (box_edit_mode == 1) {
								float pos_x = event.mouseMove.x;
								float pos_y = event.mouseMove.y;
								if ((pos_x < gt_boxes[current_highlight].r) && (pos_y > gt_boxes[current_highlight].t)) {
									gt_boxes[current_highlight].l = pos_x;
									gt_boxes[current_highlight].b = pos_y;
								}
							}
							else if (box_edit_mode == 3) {
								float pos_x = event.mouseMove.x;
								float pos_y = event.mouseMove.y;
								if ((pos_x > gt_boxes[current_highlight].l) && (pos_y > gt_boxes[current_highlight].t)) {
									gt_boxes[current_highlight].r = pos_x;
									gt_boxes[current_highlight].b = pos_y;
								}
							}
							else if (box_edit_mode == 7) {
								float pos_x = event.mouseMove.x;
								float pos_y = event.mouseMove.y;
								if ((pos_x < gt_boxes[current_highlight].r) && (pos_y < gt_boxes[current_highlight].b)) {
									gt_boxes[current_highlight].l = pos_x;
									gt_boxes[current_highlight].t = pos_y;
								}
							}
							else if (box_edit_mode == 9) {
								float pos_x = event.mouseMove.x;
								float pos_y = event.mouseMove.y;
								if ((pos_x > gt_boxes[current_highlight].l) && (pos_y < gt_boxes[current_highlight].b)) {
									gt_boxes[current_highlight].r = pos_x;
									gt_boxes[current_highlight].t = pos_y;
								}
							}
						}
					}
					if (event.type == sf::Event::MouseButtonPressed) {
						if (event.mouseButton.x > im_w + border_w) {
							if (event.mouseButton.button == sf::Mouse::Left) {
								int selected = (int)(event.mouseButton.y / class_name_h);
								if ((selected < 0) || (selected > n_class - 1)) selected = -1;
								else gt_boxes[current_highlight].class_id = selected;
							}
						}
						else {
							if (event.mouseButton.button == sf::Mouse::Left) {
								if ((box_edit_mode == 1) || (box_edit_mode == 3) || (box_edit_mode == 7) || (box_edit_mode == 9)) {
									box_edit_mode = 5;
								}
							}
							else if (event.mouseButton.button == sf::Mouse::Right) {
								if ((box_edit_mode == 1) || (box_edit_mode == 3) || (box_edit_mode == 7) || (box_edit_mode == 9)) {
									box_edit_mode = 5;
									gt_boxes[current_highlight].l = saved_l;
									gt_boxes[current_highlight].r = saved_r;
									gt_boxes[current_highlight].t = saved_t;
									gt_boxes[current_highlight].b = saved_b;
								}
								else if (box_edit_mode == 5) {
									gt_boxes[current_highlight].highlight = 0;
									current_highlight = -1;
									box_edit_mode = 5;
								}
							}
						}
					}
					if (event.type == sf::Event::KeyPressed) {
						if (box_edit_mode == 5) {
							if (event.key.code == sf::Keyboard::Numpad1 || event.key.code == sf::Keyboard::Z) {
								saved_l = gt_boxes[current_highlight].l;
								saved_r = gt_boxes[current_highlight].r;
								saved_t = gt_boxes[current_highlight].t;
								saved_b = gt_boxes[current_highlight].b;
								box_edit_mode = 1;
							}
							if (event.key.code == sf::Keyboard::Numpad3 || event.key.code == sf::Keyboard::C) {
								saved_l = gt_boxes[current_highlight].l;
								saved_r = gt_boxes[current_highlight].r;
								saved_t = gt_boxes[current_highlight].t;
								saved_b = gt_boxes[current_highlight].b;
								box_edit_mode = 3;
							}
							if (event.key.code == sf::Keyboard::Numpad7 || event.key.code == sf::Keyboard::Q) {
								saved_l = gt_boxes[current_highlight].l;
								saved_r = gt_boxes[current_highlight].r;
								saved_t = gt_boxes[current_highlight].t;
								saved_b = gt_boxes[current_highlight].b;
								box_edit_mode = 7;
							}
							if (event.key.code == sf::Keyboard::Numpad9 || event.key.code == sf::Keyboard::E) {
								saved_l = gt_boxes[current_highlight].l;
								saved_r = gt_boxes[current_highlight].r;
								saved_t = gt_boxes[current_highlight].t;
								saved_b = gt_boxes[current_highlight].b;
								box_edit_mode = 9;
							}
							if (event.key.code == sf::Keyboard::Left) {
								gt_boxes[current_highlight].highlight = 0;
								box_edit_mode = 5;
								set_prev_highlight(gt_boxes, n_box, current_highlight);
							}
							if (event.key.code == sf::Keyboard::Right) {
								gt_boxes[current_highlight].highlight = 0;
								box_edit_mode = 5;
								set_next_highlight(gt_boxes, n_box, current_highlight);
							}
							if (event.key.code == sf::Keyboard::Up) {
								gt_boxes[current_highlight].class_id = (gt_boxes[current_highlight].class_id + 1) % n_class;
							}
							if (event.key.code == sf::Keyboard::Down) {
								gt_boxes[current_highlight].class_id = (gt_boxes[current_highlight].class_id - 1) % n_class;
								gt_boxes[current_highlight].class_id = gt_boxes[current_highlight].class_id < 0 ? gt_boxes[current_highlight].class_id + n_class : gt_boxes[current_highlight].class_id;
							}
							if (event.key.code == sf::Keyboard::Equal) {
								gt_boxes[current_highlight].class_id = current_cls;
							}
							if (event.key.code == sf::Keyboard::Tilde) {
								gt_boxes[current_highlight].difficult = 1 - gt_boxes[current_highlight].difficult;
							}
							if (event.key.code == sf::Keyboard::R) {
								if (preload_gt_boxes) {
									free(gt_boxes);
									n_box = preload_n_box;
									fprintf(stderr, "Reloaded\n");
									gt_boxes = (gt_box*)calloc(n_box, sizeof(gt_box));
									memcpy(gt_boxes, preload_gt_boxes, n_box * sizeof(gt_box));
								}
							}
							if (event.key.code == sf::Keyboard::Tab) {
								window_state = 1 - window_state;
							}
							if (event.key.code == sf::Keyboard::S) {
								FILE* file = fopen(label_name, "w");
								FILE* file_diff = fopen(label_diff_name, "w");
								for (i = 0; i < n_box; ++i) {
									if (gt_boxes[i].class_id != -1) {
										float x = (gt_boxes[i].l + gt_boxes[i].r) / (2 * im_w);
										float y = (gt_boxes[i].t + gt_boxes[i].b) / (2 * im_h);
										float w = (gt_boxes[i].r - gt_boxes[i].l + 1) / im_w;
										float h = (gt_boxes[i].b - gt_boxes[i].t + 1) / im_h;
										fprintf(file, "%d %.16f %.16f %.16f %.16f\n", gt_boxes[i].class_id, x, y, w, h);
										fprintf(file_diff, "%d\n", gt_boxes[i].difficult);
									}
								}
								fclose(file);
								fclose(file_diff);
								fprintf(stderr, "Saved\n");
							}
						}
						if (event.key.code == sf::Keyboard::Escape) {
							gt_boxes[current_highlight].highlight = 0;
							current_highlight = -1;
							box_edit_mode = 5;
						}
						if (event.key.code == sf::Keyboard::Delete) {
							if (box_edit_mode == 5) {
								gt_boxes[current_highlight].class_id = -1;
								gt_boxes[current_highlight].highlight = 0;
								current_highlight = -1;
							}
						}
					}

					selected_mask.setPosition(im_w + border_w, gt_boxes[current_highlight].class_id * class_name_h);
				}
			}
		}
		//visualization
		show_window.clear(sf::Color(250, 250, 250));
		if (window_state == 0) {
			show_window.draw(spr);
			if (draw_state == 1 || draw_state == 2) {
				box_12[0] = sf::Vertex(box_1);
				box_12[1] = sf::Vertex(box_2);
				box_13[0] = sf::Vertex(box_1);
				box_13[1] = sf::Vertex(box_3);
				box_42[0] = sf::Vertex(box_4);
				box_42[1] = sf::Vertex(box_2);
				box_43[0] = sf::Vertex(box_4);
				box_43[1] = sf::Vertex(box_3);
				show_window.draw(box_12, 4, sf::Lines);
				show_window.draw(box_13, 4, sf::Lines);
				show_window.draw(box_42, 4, sf::Lines);
				show_window.draw(box_43, 4, sf::Lines);
			}
			show_window.draw(cursor_h_line, 2, sf::Lines);
			show_window.draw(cursor_v_line, 2, sf::Lines);
			sprintf(box_info_buf, "current class: %s / diff=%d", class_list[current_cls], current_difficult);
			info.setString(box_info_buf);
			show_window.draw(info);
		}
		else {
			if (current_highlight != -1) {
				sprintf(box_info_buf, "current class: %s / diff=%d                    highlight class: %s / diff=%d", class_list[current_cls], current_difficult, class_list[gt_boxes[current_highlight].class_id], gt_boxes[current_highlight].difficult);
			}
			else if (preview_highlight != -1) {
				sprintf(box_info_buf, "current class: %s / diff=%d                    preview highlight class: %s / diff=%d", class_list[current_cls], current_difficult, class_list[gt_boxes[preview_highlight].class_id], gt_boxes[preview_highlight].difficult);
			}
			else {
				sprintf(box_info_buf, "current class: %s / diff=%d", class_list[current_cls], current_difficult);
			}
			info.setString(box_info_buf);

			show_window.draw(spr);
			if (current_highlight != -1) {
				float l = gt_boxes[current_highlight].l;
				float r = gt_boxes[current_highlight].r;
				float t = gt_boxes[current_highlight].t;
				float b = gt_boxes[current_highlight].b;
				mask.setSize(sf::Vector2f(l, im_h));
				mask.setPosition(0, 0);
				show_window.draw(mask);
				mask.setSize(sf::Vector2f(r - l, t));
				mask.setPosition(l, 0);
				show_window.draw(mask);
				mask.setSize(sf::Vector2f(im_w - r, im_h));
				mask.setPosition(r, 0);
				show_window.draw(mask);
				mask.setSize(sf::Vector2f(r - l, im_h - b));
				mask.setPosition(l, b);
				show_window.draw(mask);
				render_box(&show_window, gt_boxes[current_highlight], n_class);
			}
			else {
				for (i = 0; i < n_box; ++i) {
					if (gt_boxes[i].class_id != -1) {
						if (preview_highlight == i) {
							float l = gt_boxes[preview_highlight].l;
							float r = gt_boxes[preview_highlight].r;
							float t = gt_boxes[preview_highlight].t;
							float b = gt_boxes[preview_highlight].b;
							mask.setSize(sf::Vector2f(l, im_h));
							mask.setPosition(0, 0);
							show_window.draw(mask);
							mask.setSize(sf::Vector2f(r - l, t));
							mask.setPosition(l, 0);
							show_window.draw(mask);
							mask.setSize(sf::Vector2f(im_w - r, im_h));
							mask.setPosition(r, 0);
							show_window.draw(mask);
							mask.setSize(sf::Vector2f(r - l, im_h - b));
							mask.setPosition(l, b);
							show_window.draw(mask);
							render_preview_box(&show_window, gt_boxes[i], n_class);
						}
						else {
							render_box(&show_window, gt_boxes[i], n_class);
						}
					}
				}
			}
			show_window.draw(info);
		}
		show_window.draw(border);

		for (i = 0; i < n_class; ++i) {
			sprintf(class_names_buf, "  %s", class_list[i]);
			class_names.setString(class_names_buf);
			class_names.setPosition(im_w + border_w, (i * class_name_h));
			show_window.draw(class_names);
		}
		show_window.draw(selected_mask);
		show_window.draw(class_mask);
		show_window.display();
	}
	free(gt_boxes);
}

//end_new_method

extern "C" void run_synlabel(char* image_list_name, char* class_list_name) {
	int i = 0;
	int j;

	int n_class;
	char** class_list = get_labels_with_n(class_list_name, &n_class);
	list *plist = get_paths(image_list_name);
	int n_image = 0;
	n_image = plist->size;
	fprintf(stderr, "Num classes: %d\n", n_class);
	fprintf(stderr, "Num images : %d\n", n_image);
	char **paths = (char **)list_to_array(plist);

	int total_show = 21;

	int preview_box_w = 600;
	sf::RenderWindow menu(sf::VideoMode(600 + preview_box_w, 420), "image list", sf::Style::Close);
	menu.setFramerateLimit(60);
	menu.setPosition(sf::Vector2i(100, 100));

	sf::Texture preview_img_content;
	sf::RectangleShape preview_img;
	preview_img.setPosition(580, 10);
	preview_img.setSize(sf::Vector2f(preview_box_w, 400));
	preview_img.setOutlineColor(sf::Color(100, 100, 255));
	preview_img.setOutlineThickness(4);

	sf::Font font;
	font.loadFromFile("Roboto-Regular.ttf");

	sf::Text info;
	info.setFont(font);
	info.setCharacterSize(16);
	char buf[256];
	int copy_id = -1;

	while (menu.isOpen()) {
		Event event;
		while (menu.pollEvent(event)) {
			if (event.type == sf::Event::MouseWheelMoved) {
				i = i - event.mouseWheel.delta;
				if (i < 0) i = 0;
				else if (i > n_image - 1) i = n_image - 1;
			}
			if (event.type == sf::Event::KeyPressed) {
				if (event.key.code == sf::Keyboard::Escape) {
					menu.close();
				}
				if (event.key.code == sf::Keyboard::Enter) {
					do_box_labeling_compact(paths[i], class_list, n_class);
				}
				if (event.key.code == sf::Keyboard::Up) {
					i = i - 1;
					if (i < 0) i = 0;
				}
				if (event.key.code == sf::Keyboard::Down) {
					i = i + 1;
					if (i > n_image - 1) i = n_image - 1;
				}
				if (event.key.code == sf::Keyboard::PageUp) {
					i = i - 100;
					if (i < 0) i = 0;
				}
				if (event.key.code == sf::Keyboard::PageDown) {
					i = i + 100;
					if (i > n_image - 1) i = n_image - 1;
				}
				if (event.key.code == sf::Keyboard::Home) {
					i = 0;
				}
				if (event.key.code == sf::Keyboard::End) {
					i = n_image - 1;
				}
				if (event.key.code == sf::Keyboard::C) {
					copy_id = i;
				}
				if (event.key.code == sf::Keyboard::P) {
					if (copy_id >= 0 && copy_id < n_image && copy_id != i) {
						char src_label[256];
						char dst_label[256];
						char src_diff_label[256];
						char dst_diff_label[256];
						sprintf(src_label, "%s", paths[copy_id]);
						find_replace(src_label, ".jpg", ".txt", src_label);
						find_replace(src_label, ".png", ".txt", src_label);
						find_replace(src_label, "images", "labels", src_label);
						find_replace(src_label, "JPEGImages", "labels", src_label);
						find_replace(src_label, "labels", "labels_diff", src_diff_label);

						sprintf(dst_label, "%s", paths[i]);
						find_replace(dst_label, ".jpg", ".txt", dst_label);
						find_replace(dst_label, ".png", ".txt", dst_label);
						find_replace(dst_label, "images", "labels", dst_label);
						find_replace(dst_label, "JPEGImages", "labels", dst_label);
						find_replace(dst_label, "labels", "labels_diff", dst_diff_label);

						FILE* src_file = fopen(src_label, "rb");
						FILE* src_diff_file = fopen(src_diff_label, "rb");

						if (src_file == NULL || src_diff_file == NULL) {
							fprintf(stderr, "Label file not existed\n");
						} else {
							FILE* dst_file = fopen(dst_label, "wb");
							FILE* dst_diff_file = fopen(dst_diff_label, "wb");
							fseek(src_file, 0, SEEK_END);
							long lSize = ftell(src_file);
							rewind(src_file);

							char* buffer = (char*)malloc(sizeof(char) * lSize);
							fread(buffer, 1, lSize, src_file);
							fwrite(buffer, 1, lSize, dst_file);

							free(buffer);

							fseek(src_diff_file, 0, SEEK_END);
							lSize = ftell(src_diff_file);
							rewind(src_diff_file);

							buffer = (char*)malloc(sizeof(char) * lSize);
							fread(buffer, 1, lSize, src_diff_file);
							fwrite(buffer, 1, lSize, dst_diff_file);

							fprintf(stderr, "Copy %s to %s\n", src_label, dst_label);
							fprintf(stderr, "Copy %s to %s\n", src_diff_label, dst_diff_label);
							fprintf(stderr, "pasted\n");
							fclose(src_file);
							fclose(src_diff_file);
							fclose(dst_file);
							fclose(dst_diff_file);
						}
					}
					copy_id = -1;
				}
			}
		}
		menu.clear(sf::Color(250, 250, 250));
		for (j = i - total_show / 2; j < i + total_show / 2 + 1; ++j) {
			if (j < 0 || j > n_image - 1) {
				info.setString("-");
			}
			else {
				info.setString(paths[j]);
			}

			info.setPosition(10, 18 * (j - i + total_show / 2));

			int alpha = 255 - 2 * abs(j - i) * abs(j - i);
			if (j == i) {
				info.setFillColor(sf::Color(250, 20, 120, alpha));
			}
			else {
				info.setFillColor(sf::Color(200, 50, 220, alpha));
			}

			menu.draw(info);
		}

		info.setFillColor(sf::Color(0, 150, 220));
		sprintf(buf, "[ %6d / %6d ]    COPY_ID: %6d", i + 1, n_image, copy_id);
		info.setString(buf);
		info.setPosition(10, 400);
		menu.draw(info);
		preview_img_content.loadFromFile(paths[i]);
		preview_img.setTexture(&preview_img_content);

		menu.draw(preview_img);
		menu.display();
	}
}

int main() {
	FILE* file = fopen("input", "r");
	char* list = fgetl(file);
	char* class_list = fgetl(file);
	run_synlabel(list, class_list);
	return 0;
}